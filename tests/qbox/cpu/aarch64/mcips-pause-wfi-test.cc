/* SPDX-License-Identifier: BSD-3-Clause */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <thread>

#include <systemc>

#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "cortex-a53.h"
#include "qemu-instance.h"

static bool pause_wfi_passed = false;

class PauseWfiCpu : public cpu_arm_cortexA53
{
public:
    using cpu_arm_cortexA53::cpu_arm_cortexA53;
    bool disable_idle_reconcile = false;

    void before_end_of_elaboration() override
    {
        cpu_arm_cortexA53::before_end_of_elaboration();
        if (disable_idle_reconcile) {
            // Diagnostic old-path comparison without reverting production
            // code: the previous MCIPS strategy had no end-of-loop hook.
            m_cpu.set_end_of_loop_callback([] {});
        }
    }
};

class CpuArmMcipsPauseWfiTest : public CpuTestBenchBase
{
    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst;
    sc_core::sc_vector<PauseWfiCpu> m_cpus;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    gs::async_event m_terminal;
    std::mutex m_mutex;
    std::condition_variable m_cancel;
    bool m_cancelled = false;
    std::atomic<bool> m_timed_out{false};
    std::thread m_watchdog;
    bool m_secondary_ready = false;
    bool m_primary_done = false;
    sc_core::sc_time m_start;
    cci::cci_param<bool> p_disable_reconcile{"disable_reconcile", false};

    void finish()
    {
        pause_wfi_passed = m_secondary_ready && m_primary_done && !m_timed_out.load();
        std::fprintf(stderr, "MCIPS_PAUSE_WFI secondary_ready=%d primary_done=%d timeout=%d sc=%s\n",
                     m_secondary_ready, m_primary_done, m_timed_out.load(),
                     sc_core::sc_time_stamp().to_string().c_str());
        m_terminal.async_detach_suspending();
        sc_core::sc_stop();
    }

public:
    CpuArmMcipsPauseWfiTest(const sc_core::sc_module_name& name)
        : CpuTestBenchBase(name, qemu::Target::AARCH64)
        , m_inst_manager("inst_manager")
        , m_inst("inst", &m_inst_manager, qemu::Target::AARCH64)
        , m_cpus("cpu", 2,
                 [this](const char* name, size_t) { return new PauseWfiCpu(name, m_inst); })
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst, m_cpus[0])
        , m_terminal("terminal")
    {
        for (size_t i = 0; i < m_cpus.size(); ++i) {
            m_cpus[i].p_has_el3 = false;
            m_cpus[i].p_has_el2 = false;
            m_cpus[i].p_mp_affinity = i;
            m_cpus[i].disable_idle_reconcile = p_disable_reconcile;
            m_router.add_initiator(m_cpus[i].socket);
        }
        m_router.add_initiator(m_gpi.m_initiator);
        std::ostringstream firmware;
        firmware << R"(
            ldr x1, =0x80000000
            mrs x2, mpidr_el1
            and x2, x2, #0xff
            cbnz x2, secondary
        poll:
            ldr x0, [x1]
            cbz x0, poll
            mov x0, #5000
        progress:
            subs x0, x0, #1
            b.ne progress
            mov x0, #2
            str x0, [x1]
        primary_idle:
            wfi
            b primary_idle
        secondary:
            mov x0, #1
            str x0, [x1, #8]
        )";
        // The inline MCIPS quota callback runs at TB entry. A large straight
        // line ending in WFI makes its pause request coincide with the
        // architectural halt before qemu_process_cpu_events sees either.
        for (unsigned int i = 0; i < 128; ++i) firmware << "nop\n";
        firmware << R"(
        secondary_idle:
            wfi
            b secondary_idle
        )";
        set_firmware(firmware.str().c_str());
        m_terminal.async_attach_suspending();
        SC_METHOD(finish);
        sensitive << m_terminal;
        dont_initialize();
    }

    ~CpuArmMcipsPauseWfiTest() override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cancelled = true;
        }
        m_cancel.notify_one();
        if (m_watchdog.joinable()) m_watchdog.join();
    }

    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}

    uint64_t mmio_read(int, uint64_t address, size_t length) override
    {
        TEST_ASSERT(address == 0 && length == 8);
        return m_secondary_ready;
    }

    void mmio_write(int, uint64_t address, uint64_t data, size_t length) override
    {
        TEST_ASSERT(length == 8);
        if (address == 8 && data == 1) {
            TEST_ASSERT(!m_secondary_ready);
            m_secondary_ready = true;
            m_start = sc_core::sc_time_stamp();
            std::fprintf(stderr, "MCIPS_PAUSE_WFI secondary entering long TB at %s\n",
                         m_start.to_string().c_str());
            m_watchdog = std::thread([this] {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (!m_cancel.wait_for(lock, std::chrono::milliseconds(500),
                                      [this] { return m_cancelled; })) {
                    m_timed_out.store(true);
                    m_terminal.async_notify();
                }
            });
        } else {
            TEST_ASSERT(address == 0 && data == 2 && m_secondary_ready);
            m_primary_done = true;
            m_terminal.async_notify();
        }
    }
};

int sc_main(int argc, char* argv[])
{
    const int result = run_testbench<CpuArmMcipsPauseWfiTest>(argc, argv);
    return result ? result : !pause_wfi_passed;
}
