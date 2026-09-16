/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include <systemc>

#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "cortex-a53.h"
#include "qemu-instance.h"

static bool timed_mmio_passed = false;

class DelayedReturnCpu : public cpu_arm_cortexA53
{
public:
    using cpu_arm_cortexA53::cpu_arm_cortexA53;
    std::atomic<bool> delay_return{false};
    std::atomic<bool> bounded_return{true};

    void initiator_transport_end(const sc_core::sc_time& completion) override
    {
        if (delay_return.exchange(false)) {
            // Model a descheduled host CPU after the SystemC target has
            // returned. Device events must remain bounded by its new window.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            bounded_return.store(sc_core::sc_time_stamp() <=
                                 completion + tlm_utils::tlm_quantumkeeper::get_global_quantum());
        }
        QemuCpu::initiator_transport_end(completion);
    }
};

class CpuArmMcipsTimedMmioTest : public CpuTestBenchBase
{
    static constexpr const char* FIRMWARE = R"(
        _start:
            ldr x1, =0x%08)" PRIx64 R"(
            mrs x2, mpidr_el1
            and x2, x2, #0xff
            lsl x2, x2, #3
            add x1, x1, x2
            mov x0, #0
            str x0, [x1]
            mov x0, #1
            str x0, [x1]
        idle:
            wfi
            b idle
    )";

    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst;
    sc_core::sc_vector<DelayedReturnCpu> m_cpus;
    tlm_utils::simple_target_socket<CpuArmMcipsTimedMmioTest, DEFAULT_TLM_BUSWIDTH> m_tester;
    global_peripheral_initiator m_gpi;
    gs::async_event m_terminal;
    std::mutex m_mutex;
    std::condition_variable m_cancel;
    bool m_cancelled = false;
    std::atomic<bool> m_timed_out{false};
    std::thread m_watchdog;
    unsigned int m_entered = 0;
    unsigned int m_transport_completed = 0;
    unsigned int m_cpu_resumed = 0;
    std::vector<unsigned int> m_state;
    std::vector<sc_core::sc_time> m_completion;
    bool m_failure_cleanup = false;
    cci::cci_param<bool> p_annotated{"annotated", false};

    void finish()
    {
        // Do not make this observer unsuspendable: that would hide the
        // instruction-clock/window deadlock exercised by the timed target.
        timed_mmio_passed = m_entered == m_cpus.size() &&
                            m_transport_completed == m_cpus.size() &&
                            m_cpu_resumed == m_cpus.size() && !m_timed_out.load();
        for (auto& cpu : m_cpus) timed_mmio_passed &= cpu.bounded_return.load();
        std::fprintf(stderr,
                     "MCIPS_TIMED_MMIO entered=%u target_completed=%u cpu_resumed=%u timeout=%d sc=%s\n",
                     m_entered, m_transport_completed, m_cpu_resumed,
                     m_timed_out.load(), sc_core::sc_time_stamp().to_string().c_str());
        if (m_timed_out.load() && m_cpu_resumed != m_cpus.size() && !m_failure_cleanup &&
            m_inst.is_mcips_enabled()) {
            // The failed path may leave the CPU waiting on its transport.
            // Open only this test instance's window so it can unwind; the
            // timeout remains latched, and the test must still return FAIL.
            m_failure_cleanup = true;
            m_inst.get_mcips_plugin().detach_sync_window();
            return;
        }
        m_terminal.async_detach_suspending();
        sc_core::sc_stop();
    }

public:
    CpuArmMcipsTimedMmioTest(const sc_core::sc_module_name& name)
        : CpuTestBenchBase(name, qemu::Target::AARCH64)
        , m_inst_manager("inst_manager")
        , m_inst("inst", &m_inst_manager, qemu::Target::AARCH64)
        , m_cpus("cpu", p_num_cpu,
                 [this](const char* name, size_t) { return new DelayedReturnCpu(name, m_inst); })
        , m_tester("tester")
        , m_gpi("gpi", m_inst, m_cpus[0])
        , m_terminal("terminal")
    {
        char firmware[512];
        TEST_ASSERT(m_cpus.size() >= 1 && m_cpus.size() <= 2);
        m_state.resize(m_cpus.size());
        m_completion.resize(m_cpus.size());
        m_tester.register_b_transport(this, &CpuArmMcipsTimedMmioTest::transport);
        map_target(m_tester, CpuTesterMmio::MMIO_ADDR, CpuTesterMmio::MMIO_SIZE);
        for (size_t i = 0; i < m_cpus.size(); ++i) {
            m_cpus[i].p_has_el3 = false;
            m_cpus[i].p_has_el2 = false;
            m_cpus[i].p_mp_affinity = i;
            m_router.add_initiator(m_cpus[i].socket);
        }
        m_router.add_initiator(m_gpi.m_initiator);
        std::snprintf(firmware, sizeof(firmware), FIRMWARE, CpuTesterMmio::MMIO_ADDR);
        set_firmware(firmware);
        m_terminal.async_attach_suspending();
        SC_METHOD(finish);
        sensitive << m_terminal;
        dont_initialize();
        SC_THREAD(device_ticks);
    }

    ~CpuArmMcipsTimedMmioTest() override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cancelled = true;
        }
        m_cancel.notify_one();
        if (m_watchdog.joinable()) m_watchdog.join();
    }

    void start_watchdog()
    {
        m_watchdog = std::thread([this] {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_cancel.wait_for(lock, std::chrono::milliseconds(500),
                                  [this] { return m_cancelled; })) {
                m_timed_out.store(true);
                m_terminal.async_notify();
            }
        });
    }

    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}

    void device_ticks()
    {
        // Keep real timed device work pending during the delayed host return.
        while (true) wait(sc_core::sc_time(p_quantum_ns.get_value() / 4, sc_core::SC_NS));
    }

    void transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        TEST_ASSERT(trans.is_write());
        const auto address = trans.get_address();
        const auto length = trans.get_data_length();
        uint64_t data = 0;
        TEST_ASSERT(length == sizeof(data));
        std::memcpy(&data, trans.get_data_ptr(), length);
        const size_t cpu = address / sizeof(uint64_t);
        TEST_ASSERT(cpu < m_cpus.size() && address % sizeof(uint64_t) == 0 &&
                    length == sizeof(uint64_t));
        if (data == 0) {
            TEST_ASSERT(m_state[cpu] == 0);
            m_state[cpu] = 1;
            if (++m_entered == 1) start_watchdog();
            const auto start = sc_core::sc_time_stamp() + delay;
            const sc_core::sc_time latency(4 * p_quantum_ns.get_value(), sc_core::SC_NS);
            std::fprintf(stderr, "MCIPS_TIMED_MMIO cpu=%zu target_enter sc=%s latency=%s\n",
                         cpu, start.to_string().c_str(), latency.to_string().c_str());
            m_completion[cpu] = start + latency;
            if (p_annotated) {
                delay += latency;
            } else {
                wait(delay + latency);
                delay = sc_core::SC_ZERO_TIME;
                TEST_ASSERT(sc_core::sc_time_stamp() == m_completion[cpu]);
            }
            ++m_transport_completed;
            m_state[cpu] = 2;
            m_cpus[cpu].delay_return.store(true);
        } else {
            TEST_ASSERT(data == 1 && m_state[cpu] == 2);
            TEST_ASSERT(sc_core::sc_time_stamp() + delay >= m_completion[cpu]);
            m_state[cpu] = 3;
            if (++m_cpu_resumed == m_cpus.size()) m_terminal.async_notify();
        }
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

constexpr const char* CpuArmMcipsTimedMmioTest::FIRMWARE;

int sc_main(int argc, char* argv[])
{
    const int result = run_testbench<CpuArmMcipsTimedMmioTest>(argc, argv);
    return result ? result : !timed_mmio_passed;
}
