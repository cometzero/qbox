/* SPDX-License-Identifier: BSD-3-Clause */

#include <cstdio>
#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "cortex-m55.h"

class WfxCpu : public cpu_arm_cortexM55
{
public:
    using cpu_arm_cortexM55::cpu_arm_cortexM55;
    uint32_t run_state()
    {
        m_inst.get().lock_iothread();
        const auto state = m_inst.get().plugin_api().cpu_get_run_state(m_cpu.get_qemu_obj());
        m_inst.get().unlock_iothread();
        return state;
    }
};

class CortexM55WfxTest : public CpuTestBenchBase
{
    QemuInstanceManager m_manager;
    QemuInstance m_inst;
    WfxCpu m_cpu;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> m_irqs;
    unsigned m_stage = 0;

    void assemble(const char* source, uint64_t address)
    {
        ks_engine* ks;
        uint8_t* bytes;
        size_t size, count;
        TEST_ASSERT(ks_open(KS_ARCH_ARM, KS_MODE_THUMB, &ks) == KS_ERR_OK);
        TEST_ASSERT(ks_asm(ks, source, address, &bytes, &size, &count) == KS_ERR_OK);
        m_mem.load.ptr_load(bytes, address, size);
        ks_free(bytes);
        ks_close(ks);
    }

    void await_halt(unsigned stage)
    {
        const auto deadline = sc_core::sc_time_stamp() + sc_core::sc_time(2, sc_core::SC_MS);
        while (sc_core::sc_time_stamp() < deadline) {
            wait(1, sc_core::SC_US);
            TEST_ASSERT(m_stage <= stage);
            if (m_stage == stage && (m_cpu.run_state() & (1u << 3))) {
                std::fprintf(stderr, "M55_WFX halted stage=%u at %s\n", stage,
                             sc_core::sc_time_stamp().to_string().c_str());
                return;
            }
        }
        TEST_FAIL("Cortex-M did not reach expected architectural halt");
    }

    void remains_halted(unsigned stage)
    {
        wait(500, sc_core::SC_US);
        TEST_ASSERT(m_stage == stage);
        TEST_ASSERT(m_cpu.run_state() & (1u << 3));
    }

    void exercise()
    {
        await_halt(1);
        remains_halted(1); // SVC/SEV events before WFI are not WFI wakeups.
        m_irqs[0]->write(true);
        await_halt(3); // First WFE consumed the event; second WFE sleeps.
        remains_halted(3);
        m_irqs[1]->write(true); // Disabled IRQ with SEVONPEND: event only.
        await_halt(5);
        remains_halted(5);
        m_irqs[2]->write(true); // Event arriving during WFI must not wake it.
        remains_halted(5);
        m_irqs[0]->write(true);
        await_halt(6);
        std::fprintf(stderr, "M55_WFX PASS: WFI ignores events, WFE consumes/wakes, IRQ wakes WFI\n");
        sc_core::sc_stop();
    }

public:
    CortexM55WfxTest(const sc_core::sc_module_name& name)
        : CpuTestBenchBase(name, qemu::Target::AARCH64)
        , m_manager("manager")
        , m_inst("inst", &m_manager, qemu::Target::AARCH64)
        , m_cpu("cpu", m_inst)
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst, m_cpu)
        , m_irqs("irq", 3)
    {
        m_router.add_initiator(m_cpu.socket);
        m_router.add_initiator(m_gpi.m_initiator);
        m_router.add_target(m_cpu.m_nvic.socket, 0xe000e000, 0x21000);
        for (unsigned i = 0; i < 3; ++i) m_irqs[i].bind(m_cpu.m_nvic.irq_in[i]);
        uint32_t vectors[19] = {};
        vectors[0] = 0x20000;
        vectors[1] = 0x101;
        vectors[11] = vectors[16] = 0x301;
        m_mem.load.ptr_load(reinterpret_cast<uint8_t*>(vectors), 0, sizeof(vectors));
        assemble(R"(
            ldr r1, =0xe000e100
            movs r0, #1
            str r0, [r1]
            ldr r1, =0xe000ed10
            movs r0, #16
            str r0, [r1]
            ldr r1, =0x80000000
            svc #0
            sev
            movs r0, #1
            str r0, [r1]
            wfi
            movs r0, #2
            str r0, [r1]
            wfe
            movs r0, #3
            str r0, [r1]
            wfe
            movs r0, #4
            str r0, [r1]
            wfe
            movs r0, #5
            str r0, [r1]
            wfi
            movs r0, #6
            str r0, [r1]
        final:
            wfi
            b final
        )", 0x100);
        assemble(R"(
            push {r0, r1}
            ldr r1, =0x80000008
            movs r0, #1
            str r0, [r1]
            pop {r0, r1}
            bx lr
        )", 0x300);
        SC_THREAD(exercise);
    }

    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}
    void mmio_write(int, uint64_t address, uint64_t value, size_t length) override
    {
        TEST_ASSERT(length == 4);
        if (address == 8) {
            m_irqs[0]->write(false);
            return;
        }
        TEST_ASSERT(address == 0 && value == m_stage + 1);
        m_stage = value;
        std::fprintf(stderr, "M55_WFX stage=%u at %s\n", m_stage,
                     sc_core::sc_time_stamp().to_string().c_str());
    }
};

int sc_main(int argc, char* argv[])
{
    return run_testbench<CortexM55WfxTest>(argc, argv);
}
