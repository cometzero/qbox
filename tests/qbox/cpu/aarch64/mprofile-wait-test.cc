/* SPDX-License-Identifier: BSD-3-Clause */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "cortex-m55.h"

/* Exercise real Thumb WFx instructions, including their NVIC wake conditions. */
class MProfileWaitTest : public CpuTestBenchBase
{
    QemuInstanceManager m_manager;
    QemuInstance m_inst;
    cpu_arm_cortexM55 m_cpu;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    InitiatorSignalSocket<bool> m_irq, m_event_irq;
    sc_core::sc_out<bool> m_reset;
    gs::async_event m_keepalive, m_heartbeat;
    std::atomic<unsigned> m_phase{0};
    std::atomic<bool> m_irq_asserted{false};
    std::mutex m_mutex;
    std::condition_variable m_stop;
    bool m_stopping = false;
    std::thread m_clock;

    void load_firmware()
    {
        /* PRIMASK prevents exception entry, but an enabled pending IRQ wakes WFI.
         * IRQ 1 stays disabled: SEVONPEND may wake WFE, never WFI. */
        const char* assembly = R"(
            cpsid i
            ldr r4, =0x80000000
            ldr r5, =0xe000e280
            ldr r6, =0xe000e100
            movs r0, #1
            str r0, [r6]
            sev
            movs r0, #1
            str r0, [r4]
            wfi
            bl irq_released
            movs r0, #1
            str r0, [r5]
            wfe
            ldr r6, =0xe000ed10
            movs r0, #16
            str r0, [r6]
            movs r0, #2
            str r0, [r4]
            wfe
            bl irq_released
            movs r0, #2
            str r0, [r5]
            movs r0, #3
            str r0, [r4]
            wfe
            bl irq_released
            movs r0, #2
            str r0, [r5]
            sev
            movs r0, #4
            str r0, [r4]
            wfi
            movs r0, #5
            str r0, [r4]
        done:
            b done
        irq_released:
            ldr r0, [r4, #4]
            cmp r0, #0
            bne irq_released
            bx lr
        )";
        ks_engine* ks = nullptr;
        uint8_t* bytes = nullptr;
        size_t size = 0, count = 0;
        TEST_ASSERT(ks_open(KS_ARCH_ARM, KS_MODE_THUMB, &ks) == KS_ERR_OK);
        TEST_ASSERT(ks_asm(ks, assembly, 0x100, &bytes, &size, &count) == KS_ERR_OK);
        TEST_ASSERT(size != 0);
        m_mem.load.ptr_load(bytes, 0x100, size);
        ks_free(bytes);
        ks_close(ks);
        uint8_t vectors[] = {0x00, 0xf0, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00};
        m_mem.load.ptr_load(vectors, 0, sizeof(vectors));
    }

    void await_phase(unsigned phase)
    {
        for (unsigned i = 0; i != 100 && m_phase.load() != phase; ++i) {
            TEST_ASSERT(m_phase.load() < phase);
            wait(m_heartbeat);
        }
        TEST_ASSERT(m_phase.load() == phase);
    }

    void remains_asleep(unsigned phase)
    {
        /* Wall-clock observations avoid a simulator's idle time jump hiding a
         * guest which has not reached WFx yet. Five heartbeats cover 100 ms. */
        for (unsigned i = 0; i != 5; ++i) {
            wait(m_heartbeat);
            TEST_ASSERT(m_phase.load() == phase);
        }
    }

    void pulse(InitiatorSignalSocket<bool>& irq)
    {
        /* Keep the guest from clearing a level IRQ while it is asserted. */
        m_irq_asserted.store(true);
        irq->write(true);
        wait(m_heartbeat);
        irq->write(false);
        wait(m_heartbeat);
        m_irq_asserted.store(false);
    }

    void observe()
    {
        sc_core::sc_unsuspendable();
        m_clock = std::thread([this] {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (!m_stop.wait_for(lock, std::chrono::milliseconds(20),
                                   [this] { return m_stopping; })) {
                m_heartbeat.async_notify();
            }
        });
        await_phase(1);
        remains_asleep(1); // A sticky SEV event must not bypass WFI.
        pulse(m_irq);
        await_phase(2);
        remains_asleep(2); // WFI preserved the event for the first WFE to consume.
        pulse(m_event_irq);
        await_phase(3);
        remains_asleep(3); // Completing the prior WFE consumed SEVONPEND.
        pulse(m_event_irq);
        await_phase(4);
        remains_asleep(4); // Switching back from WFE to WFI ignores a new SEV.
        pulse(m_event_irq);
        remains_asleep(4); // A disabled IRQ event also cannot wake WFI.
        pulse(m_irq);
        await_phase(5);
        m_cpu.halt_cb(true);
        m_keepalive.async_detach_suspending();
        SCP_INFO(SCMOD) << "M55_WFX_PASS sticky-event-wfi irq-wake sev-consume sevonpend-wake";
        sc_core::sc_stop();
        sc_core::sc_suspendable();
    }

public:
    MProfileWaitTest(const sc_core::sc_module_name& n)
        : CpuTestBenchBase(n, qemu::Target::AARCH64)
        , m_manager("manager")
        , m_inst("inst", &m_manager, qemu::Target::AARCH64)
        , m_cpu("cpu", m_inst)
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst, m_cpu)
        , m_irq("irq")
        , m_event_irq("event_irq")
        , m_reset("reset")
        , m_keepalive("keepalive")
        , m_heartbeat("heartbeat")
    {
        m_keepalive.async_attach_suspending();
        m_cpu.p_start_in_reset = true;
        m_cpu.p_reset_power_on = true;
        m_reset.bind(m_cpu.reset);
        m_router.add_initiator(m_cpu.socket);
        m_router.add_initiator(m_gpi.m_initiator);
        m_router.add_target(m_cpu.m_nvic.socket, 0xe000e000, 0x21000);
        m_irq.bind(m_cpu.m_nvic.irq_in[0]);
        m_event_irq.bind(m_cpu.m_nvic.irq_in[1]);
        load_firmware();
        SC_THREAD(observe);
        m_cpu.reset_cb(false);
    }

    ~MProfileWaitTest() override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_stop.notify_all();
        if (m_clock.joinable()) m_clock.join();
    }

    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}

    uint64_t mmio_read(int, uint64_t addr, size_t len) override
    {
        TEST_ASSERT(addr == 4 && len == 4);
        return m_irq_asserted.load();
    }

    void mmio_write(int, uint64_t addr, uint64_t data, size_t len) override
    {
        TEST_ASSERT(addr == 0 && len == 4);
        TEST_ASSERT(data == m_phase.load() + 1);
        std::cerr << "M55_WFX_PHASE=" << data << "\n";
        m_phase.store(data);
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<MProfileWaitTest>(argc, argv); }
