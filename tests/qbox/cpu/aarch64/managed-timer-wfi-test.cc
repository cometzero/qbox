/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <systemc>

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "arm_gicv3.h"
#include "cortex-a53.h"
#include "qemu_arm_arch_timer_mmio.h"
#include "qemu-instance.h"

class CpuArmManagedTimerWfiTest : public CpuTestBenchBase
{
    enum class TerminalOutcome {
        pending,
        isr_resume,
        unexpected,
        watchdog_failure,
        baseline_no_resume,
    };

    static constexpr uint64_t GICD_BASE = 0x08000000;
    static constexpr uint64_t GICD_SIZE = 0x00010000;
    static constexpr uint64_t GICR_BASE = 0x080a0000;
    static constexpr uint64_t GICR_SIZE = 0x00020000;
    static constexpr uint64_t TIMER_BASE = 0x2a800000;
    static constexpr uint64_t TIMER_SIZE = 0x00020000;
    static constexpr unsigned int TIMER_SPI = 49;
    static constexpr unsigned int TIMER_INTID = 32 + TIMER_SPI;

    static constexpr const char* FIRMWARE = R"(
        _start:
            ldr x10, =0x%08)" PRIx64 R"(
            ldr x11, =0x%08)" PRIx64 R"(
            ldr x12, =0x%08)" PRIx64 R"(
            ldr x13, =0x%08)" PRIx64 R"(

            msr spsel, #1
            adr x0, vectors
            msr vbar_el1, x0

            mov x0, #1
            msr icc_sre_el1, x0
            isb
            mov x0, #0xff
            msr icc_pmr_el1, x0
            mov x0, #1
            msr icc_igrpen1_el1, x0

            mov x0, #2
            str w0, [x11]
            mov x0, #(1 << 17)
            str w0, [x11, #0x88]
            str w0, [x11, #0x108]
            mov x0, #0
            str x0, [x11, #0x6288]

            mov x0, #1
            str x0, [x10]

            ldr x0, =%u
            cbz x0, idle
            str w0, [x13, #0x28]
            mov x0, #1
            str w0, [x13, #0x2c]
            mov x0, #4
            str x0, [x10]
            msr daifclr, #2
        idle:
            wfi
            b idle

        .balign 2048
        vectors:
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b irq_handler
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected
        .balign 128
            b unexpected

        irq_handler:
            mrs x0, icc_iar1_el1
            cmp x0, #%u
            b.ne unexpected
            mov x1, #0
            str w1, [x13, #0x2c]
            mov x1, #2
            str x1, [x10]
            msr icc_eoir1_el1, x0
            eret

        unexpected:
            mov x0, #3
            str x0, [x10]
            b unexpected
    )";

    cci::cci_param<uint64_t> p_timer_ticks;
    cci::cci_param<bool> p_expect_timer_wake;
    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst;
    cpu_arm_cortexA53 m_cpu;
    arm_gicv3 m_gic;
    qemu_arm_arch_timer_mmio m_timer;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    sc_core::sc_out<bool> m_reset;
    unsigned int m_entered = 0;
    unsigned int m_resumed = 0;
    unsigned int m_programmed = 0;
    unsigned int m_unexpected = 0;
    gs::async_event m_keepalive;
    gs::async_event m_progress;
    gs::async_event m_terminal;
    gs::async_event m_watchdog_timeout;
    std::mutex m_watchdog_mutex;
    std::condition_variable m_watchdog_cancel;
    bool m_watchdog_cancelled = false;
    std::atomic<TerminalOutcome> m_terminal_outcome{ TerminalOutcome::pending };
    std::atomic_bool m_watchdog_fired{ false };
    std::thread m_watchdog;

public:
    CpuArmManagedTimerWfiTest(const sc_core::sc_module_name& n)
        : CpuTestBenchBase(n, qemu::Target::AARCH64)
        , p_timer_ticks("timer_ticks", 0, "Architectural timer delay in counter ticks")
        , p_expect_timer_wake("expect_timer_wake", false, "Expect the timer IRQ handler to run")
        , m_inst_manager("inst_manager")
        , m_inst("inst", &m_inst_manager, qemu::Target::AARCH64)
        , m_cpu("cpu", m_inst)
        , m_gic("gic", m_inst, 1)
        , m_timer("timer", m_inst)
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst, m_cpu)
        , m_reset("reset")
        , m_keepalive("keepalive")
        , m_progress("progress")
        , m_terminal("terminal")
        , m_watchdog_timeout("watchdog_timeout")
    {
        char firmware[8192];

        TEST_ASSERT(m_inst.manages_start_in_reset_release());
        TEST_ASSERT(m_gic.redist_iface.size() == 1);
        m_keepalive.async_attach_suspending();

        m_cpu.p_mp_affinity = 0;
        m_cpu.p_has_el3 = false;
        m_cpu.p_has_el2 = false;
        m_cpu.p_start_in_reset = true;
        m_cpu.p_reset_power_on = true;
        m_cpu.p_cntfrq_hz = 100000000;
        m_reset.bind(m_cpu.reset);

        m_router.add_initiator(m_cpu.socket);
        m_router.add_initiator(m_gpi.m_initiator);
        m_router.add_target(m_gic.dist_iface, GICD_BASE, GICD_SIZE);
        m_router.add_target(m_gic.redist_iface[0], GICR_BASE, GICR_SIZE);
        m_router.add_target(m_timer.socket, TIMER_BASE, TIMER_SIZE);

        m_cpu.irq_timer_phys_out.bind(m_gic.ppi_in[0][30]);
        m_cpu.irq_timer_virt_out.bind(m_gic.ppi_in[0][27]);
        m_cpu.irq_timer_hyp_out.bind(m_gic.ppi_in[0][26]);
        m_cpu.irq_timer_sec_out.bind(m_gic.ppi_in[0][29]);
        m_timer.irq[0].bind(m_gic.spi_in[TIMER_SPI]);
        m_gic.irq_out[0].bind(m_cpu.irq_in);
        m_gic.fiq_out[0].bind(m_cpu.fiq_in);
        m_gic.virq_out[0].bind(m_cpu.virq_in);
        m_gic.vfiq_out[0].bind(m_cpu.vfiq_in);

        std::snprintf(firmware, sizeof(firmware), FIRMWARE,
                      CpuTesterMmio::MMIO_ADDR, GICD_BASE,
                      GICR_BASE + 0x10000, TIMER_BASE + 0x10000,
                      static_cast<unsigned int>(p_timer_ticks.get_value()),
                      TIMER_INTID);
        set_firmware(firmware);

        SC_THREAD(observe);
        m_cpu.reset_cb(false);
    }

    ~CpuArmManagedTimerWfiTest() override
    {
        cancel_watchdog();
    }

    void start_watchdog()
    {
        m_watchdog_fired.store(false);
        m_watchdog = std::thread([this] {
            std::unique_lock<std::mutex> lock(m_watchdog_mutex);

            if (!m_watchdog_cancel.wait_for(lock, std::chrono::seconds(2),
                                             [this] { return m_watchdog_cancelled; })) {
                TerminalOutcome pending = TerminalOutcome::pending;
                if (m_terminal_outcome.compare_exchange_strong(
                        pending, TerminalOutcome::watchdog_failure)) {
                    m_watchdog_fired.store(true);
                    m_watchdog_timeout.async_notify();
                }
            }
        });
    }

    void cancel_watchdog()
    {
        {
            std::lock_guard<std::mutex> lock(m_watchdog_mutex);
            m_watchdog_cancelled = true;
        }
        m_watchdog_cancel.notify_one();
        if (m_watchdog.joinable()) {
            m_watchdog.join();
        }
    }

    void wait_for_terminal_outcome()
    {
        if (m_terminal_outcome.load() == TerminalOutcome::pending) {
            start_watchdog();
            while (m_terminal_outcome.load() == TerminalOutcome::pending) {
                wait(m_terminal | m_watchdog_timeout);
            }
            cancel_watchdog();
        }
    }

    void observe()
    {
        sc_core::sc_unsuspendable();
        wait(m_progress);
        if (p_timer_ticks.get_value() != 0) {
            wait(m_progress);
            TEST_ASSERT(m_programmed == 1);
        }
        wait_for_terminal_outcome();
        if (!p_expect_timer_wake.get_value() &&
            m_terminal_outcome.load() == TerminalOutcome::watchdog_failure) {
            m_terminal_outcome.store(TerminalOutcome::baseline_no_resume);
        }

        uint64_t timer_ctl = 0;
        uint64_t gic_enabled = 0;
        uint64_t gic_pending = 0;
        qemu::MemoryRegionOps::MemTxAttrs attrs = {};
        m_inst.get().lock_iothread();
        m_gpi.m_initiator.qemu_io_read(TIMER_BASE + 0x10000 + 0x2c,
                                       &timer_ctl, sizeof(uint32_t), attrs);
        m_gpi.m_initiator.qemu_io_read(GICD_BASE + 0x108,
                                       &gic_enabled, sizeof(uint32_t), attrs);
        m_gpi.m_initiator.qemu_io_read(GICD_BASE + 0x208,
                                       &gic_pending, sizeof(uint32_t), attrs);
        m_inst.get().unlock_iothread();
        SCP_INFO(SCMOD) << "QBOX_TIMER_WFI_STATE timer_ctl=" << timer_ctl
                        << " gic_enabled=" << gic_enabled
                        << " gic_pending=" << gic_pending;

        m_keepalive.async_detach_suspending();
        m_cpu.halt_cb(true);
        TEST_ASSERT(m_entered == 1);
        TEST_ASSERT(m_programmed == (p_timer_ticks.get_value() == 0 ? 0 : 1));
        TEST_ASSERT(m_unexpected == 0);
        if (p_expect_timer_wake.get_value()) {
            TEST_ASSERT(!m_watchdog_fired.load());
            TEST_ASSERT(m_terminal_outcome.load() == TerminalOutcome::isr_resume);
            TEST_ASSERT(m_resumed == 1);
            SCP_INFO(SCMOD) << "QBOX_TIMER_WFI_RESUME spi=49 intid=81 entered=1 resumed=1";
        } else {
            TEST_ASSERT(m_watchdog_fired.load());
            TEST_ASSERT(m_terminal_outcome.load() == TerminalOutcome::baseline_no_resume);
            TEST_ASSERT(m_resumed == 0);
            SCP_INFO(SCMOD) << "QBOX_WFI_BASELINE_PASS entered=1 resumed=0";
        }
        sc_core::sc_stop();
        sc_core::sc_suspendable();
    }

    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}

    void mmio_write(int, uint64_t, uint64_t data, size_t) override
    {
        if (data == 1) {
            ++m_entered;
            m_progress.async_notify();
        } else if (data == 2) {
            ++m_resumed;
            m_terminal_outcome.store(TerminalOutcome::isr_resume);
            m_terminal.async_notify();
        } else if (data == 4) {
            ++m_programmed;
            m_progress.async_notify();
        } else {
            ++m_unexpected;
            m_terminal_outcome.store(TerminalOutcome::unexpected);
            m_terminal.async_notify();
        }
    }

    uint64_t mmio_read(int, uint64_t, size_t) override
    {
        TEST_FAIL("Unexpected CPU read");
    }

    bool dmi_request(int, uint64_t, size_t, tlm::tlm_dmi&) override
    {
        TEST_FAIL("Unexpected DMI request");
    }
};

constexpr const char* CpuArmManagedTimerWfiTest::FIRMWARE;

int sc_main(int argc, char* argv[])
{
    return run_testbench<CpuArmManagedTimerWfiTest>(argc, argv);
}
