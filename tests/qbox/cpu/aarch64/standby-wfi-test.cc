/* SPDX-License-Identifier: BSD-3-Clause */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <systemc>

#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "cortex-a53.h"
#include "qemu-instance.h"

static bool standby_passed = false;

class StandbyObservedCpu : public cpu_arm_cortexA53
{
public:
    using cpu_arm_cortexA53::cpu_arm_cortexA53;
    uint64_t run_state(uintptr_t* pc = nullptr)
    {
        m_inst.get().lock_iothread();
        const auto state = m_inst.get().plugin_api().cpu_get_run_state(m_cpu.get_qemu_obj());
        if (pc) *pc = m_cpu.get_pc();
        m_inst.get().unlock_iothread();
        return state;
    }
};

class CpuArmStandbyWfiTest : public CpuTestBenchBase
{
    QemuInstanceManager m_manager;
    QemuInstance m_inst;
    StandbyObservedCpu m_cpu;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    TargetSignalSocket<bool> m_standby;
    sc_core::sc_out<bool> m_irq;
    sc_core::sc_out<bool> m_reset;
    sc_core::sc_out<bool> m_halt;
    gs::async_event m_mmio{false};
    gs::async_event m_abort{false};
    gs::async_event m_keepalive{true};
    std::mutex m_mutex;
    std::condition_variable m_cancel;
    bool m_cancelled = false;
    std::thread m_watchdog;
    unsigned m_phase = 0;
    unsigned m_boots = 0;
    unsigned m_scheduler_pauses = 0;
    unsigned m_standby_rises = 0;
    bool m_reset_asserted = false;
    bool m_halt_asserted = false;
    bool m_done = false;
    cci::cci_param<bool> p_reset_feedback{"reset_feedback", false};
    bool m_feedback_reset = false;
    bool m_feedback_low = false;
    sc_core::sc_time m_feedback_time;
    uint64_t m_feedback_delta = 0;

public:
    CpuArmStandbyWfiTest(const sc_core::sc_module_name& name)
        : CpuTestBenchBase(name, qemu::Target::AARCH64)
        , m_manager("manager")
        , m_inst("inst", &m_manager, qemu::Target::AARCH64)
        , m_cpu("cpu", m_inst)
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst, m_cpu)
        , m_standby("standby")
        , m_irq("irq")
        , m_reset("reset")
        , m_halt("halt")
    {
        m_cpu.p_has_el3 = false;
        m_cpu.p_has_el2 = false;
        m_cpu.standby_wfi.bind(m_standby);
        m_standby.register_value_changed_cb([this](const bool& value) {
            if (value) {
                check_standby();
                if (p_reset_feedback && !m_feedback_reset) {
                    m_feedback_reset = true;
                    m_feedback_time = sc_core::sc_time_stamp();
                    m_feedback_delta = sc_core::sc_delta_count();
                    m_reset_asserted = true;
                    // Like host_ppu, synchronously feed standby back into
                    // reset from the publisher's SystemC process.
                    m_reset.write(true);
                }
            } else if (m_feedback_reset && !m_feedback_low) {
                TEST_ASSERT(sc_core::sc_time_stamp() == m_feedback_time);
                TEST_ASSERT(sc_core::sc_delta_count() == m_feedback_delta + 1);
                m_feedback_low = true;
                std::fprintf(stderr, "STANDBY_FEEDBACK low same_time=1 delta_gap=%llu\n",
                             static_cast<unsigned long long>(sc_core::sc_delta_count() - m_feedback_delta));
            }
        });
        m_irq.bind(m_cpu.irq_in);
        m_reset.bind(m_cpu.reset);
        m_halt.bind(m_cpu.halt);
        m_router.add_initiator(m_cpu.socket);
        m_router.add_initiator(m_gpi.m_initiator);
        set_firmware(R"(
            ldr x1, =0x80000000
            mov x0, #0
            str x0, [x1]
            mov x0, #5000
        busy:
            subs x0, x0, #1
            b.ne busy
            mov x0, #1
            str x0, [x1]
            wfi
            mov x0, #2
            str x0, [x1]
        idle:
            wfi
            b idle
        )");
        SC_THREAD(control);
        SC_THREAD(observe_scheduler);
        SC_METHOD(abort_test);
        sensitive << m_abort;
        dont_initialize();
    }

    ~CpuArmStandbyWfiTest() override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cancelled = true;
        }
        m_cancel.notify_one();
        if (m_watchdog.joinable()) m_watchdog.join();
    }

    void abort_test()
    {
        std::fprintf(stderr, "STANDBY_WFI timeout phase=%u boots=%u rises=%u pauses=%u\n",
                     m_phase, m_boots, m_standby_rises, m_scheduler_pauses);
        m_done = true;
        m_keepalive.async_detach_suspending();
        sc_core::sc_stop();
    }

    void check_standby()
    {
        if (m_standby.read()) {
            TEST_ASSERT(m_phase >= 1 && !m_reset_asserted && !m_halt_asserted);
            uintptr_t pc = 0;
            const auto state = m_cpu.run_state(&pc);
            std::fprintf(stderr, "STANDBY_WFI high phase=%u run_state=0x%llx pc=0x%llx irq=%d\n",
                         m_phase, static_cast<unsigned long long>(state),
                         static_cast<unsigned long long>(pc), m_irq.read());
            TEST_ASSERT((state & (1U << 3)) && !(state & 0x33U));
            ++m_standby_rises;
        }
    }

    void observe_scheduler()
    {
        while (!m_done) {
            wait(sc_core::sc_time(p_quantum_ns.get_value(), sc_core::SC_NS));
            const auto state = m_cpu.run_state();
            if ((state & 3U) && !(state & (1U << 3))) {
                TEST_ASSERT(!m_standby.read());
                ++m_scheduler_pauses;
            }
        }
    }

    void control()
    {
        if (p_reset_feedback) {
            while (!m_feedback_low) wait(m_standby->value_changed_event());
            TEST_ASSERT(m_reset_asserted && !m_standby.read());
            wait(sc_core::SC_ZERO_TIME);
            TEST_ASSERT(!m_standby.read());
            m_reset_asserted = false;
            m_reset.write(false);
            while (m_boots != 2) wait(m_mmio);
            while (!m_standby.read()) wait(m_standby->value_changed_event());
            TEST_ASSERT(m_phase == 1 && m_standby_rises == 2);
            standby_passed = true;
            m_done = true;
            std::fprintf(stderr, "STANDBY_FEEDBACK PASS boots=%u rises=%u\n", m_boots, m_standby_rises);
            m_keepalive.async_detach_suspending();
            sc_core::sc_stop();
            return;
        }
        while (!m_standby.read()) wait(m_standby->value_changed_event());
        TEST_ASSERT(m_phase == 1);
        m_irq.write(true);
        while (m_standby.read()) wait(m_standby->value_changed_event());
        while (m_phase != 2) wait(m_mmio);
        TEST_ASSERT(!m_standby.read());
        m_irq.write(false);
        while (!m_standby.read()) wait(m_standby->value_changed_event());
        m_halt_asserted = true;
        m_halt.write(true);
        while (m_standby.read()) wait(m_standby->value_changed_event());
        wait(sc_core::sc_time(2 * p_quantum_ns.get_value(), sc_core::SC_NS));
        TEST_ASSERT(!m_standby.read());
        m_halt_asserted = false;
        m_halt.write(false);
        while (!m_standby.read()) wait(m_standby->value_changed_event());
        m_reset_asserted = true;
        m_reset.write(true);
        while (m_standby.read()) wait(m_standby->value_changed_event());
        wait(sc_core::SC_ZERO_TIME);
        TEST_ASSERT(!m_standby.read());
        m_reset_asserted = false;
        m_reset.write(false);
        while (m_boots != 2) wait(m_mmio);
        TEST_ASSERT(!m_standby.read());
        // Finish after the restarted CPU is quiescent, not while its first
        // MMIO still owns an outstanding SystemC transport completion.
        while (!m_standby.read()) wait(m_standby->value_changed_event());
        TEST_ASSERT(m_phase == 1);
        if (m_inst.is_mcips_enabled()) TEST_ASSERT(m_scheduler_pauses != 0);
        TEST_ASSERT(m_standby_rises >= 2);
        standby_passed = true;
        m_done = true;
        std::fprintf(stderr, "STANDBY_WFI PASS boots=%u rises=%u scheduler_pauses=%u\n",
                     m_boots, m_standby_rises, m_scheduler_pauses);
        m_keepalive.async_detach_suspending();
        sc_core::sc_stop();
    }

    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}

    void mmio_write(int, uint64_t address, uint64_t data, size_t length) override
    {
        TEST_ASSERT(address == 0 && length == 8 && data <= 2);
        m_phase = data;
        if (data == 0 && ++m_boots == 1) {
            m_watchdog = std::thread([this] {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (!m_cancel.wait_for(lock, std::chrono::milliseconds(500),
                                      [this] { return m_cancelled; })) m_abort.async_notify();
            });
        }
        m_mmio.async_notify();
    }
};

int sc_main(int argc, char* argv[])
{
    const int result = run_testbench<CpuArmStandbyWfiTest>(argc, argv);
    return result ? result : !standby_passed;
}
