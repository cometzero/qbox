/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <array>
#include <cstdio>

#include <systemc>

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "cortex-a53.h"
#include "qemu-instance.h"

class CpuArmStartInResetReleaseTest : public CpuTestBenchBase
{
    static constexpr size_t CPU_COUNT = 4;
    static constexpr const char* FIRMWARE = R"(
        _start:
            ldr x1, =0x%08)" PRIx64 R"(
            mrs x2, mpidr_el1
            and x2, x2, #0xff
            lsl x0, x2, #3
            add x1, x1, x0
            mov x0, #1
            str x0, [x1]
            cbz x2, busy
        end:
            wfi
            b end
        busy:
            add x3, x3, #1
            b busy
    )";

    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst;
    sc_core::sc_vector<cpu_arm_cortexA53> m_cpus;
    CpuTesterMmio m_tester;
    global_peripheral_initiator m_gpi;
    sc_core::sc_vector<sc_core::sc_out<bool>> m_reset;
    std::array<unsigned int, CPU_COUNT> m_writes{};
    gs::async_event m_keepalive;
    gs::async_event m_cpu_write;

public:
    CpuArmStartInResetReleaseTest(const sc_core::sc_module_name& n)
        : CpuTestBenchBase(n, qemu::Target::AARCH64)
        , m_inst_manager("inst_manager")
        , m_inst("inst", &m_inst_manager, qemu::Target::AARCH64)
        , m_cpus("cpu", CPU_COUNT,
                 [this](const char* name, size_t) {
                     return new cpu_arm_cortexA53(name, m_inst);
                 })
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst, m_cpus[0])
        , m_reset("reset", CPU_COUNT)
        , m_keepalive("keepalive")
        , m_cpu_write("cpu_write")
    {
        char firmware[512];

        TEST_ASSERT(m_inst.manages_start_in_reset_release());
        m_keepalive.async_attach_suspending();

        for (size_t i = 0; i < m_cpus.size(); ++i) {
            auto& cpu = m_cpus[i];
            cpu.p_mp_affinity = i;
            cpu.p_has_el3 = false;
            cpu.p_has_el2 = false;
            cpu.p_start_powered_off = i != 0;
            cpu.p_start_in_reset = true;
            cpu.p_reset_power_on = true;
            m_reset[i].bind(cpu.reset);
            m_router.add_initiator(cpu.socket);
        }
        m_router.add_initiator(m_gpi.m_initiator);

        std::snprintf(firmware, sizeof(firmware), FIRMWARE, CpuTesterMmio::MMIO_ADDR);
        set_firmware(firmware);

        SC_THREAD(release_cpus);
    }

    void release_cpus()
    {
        sc_core::sc_unsuspendable();
        wait(sc_core::SC_ZERO_TIME);

        m_reset[0].write(false);
        wait(m_cpu_write);
        TEST_ASSERT(m_writes[0] == 1);
        TEST_ASSERT(total_writes() == 1);

        for (size_t i = 1; i < m_reset.size(); ++i) {
            m_reset[i].write(false);
        }
        while (total_writes() != m_writes.size()) {
            wait(m_cpu_write);
        }
        for (size_t i = 1; i < m_writes.size(); ++i) {
            TEST_ASSERT(m_writes[i] == 1);
        }

        for (auto& reset : m_reset) {
            reset.write(true);
        }
        wait(sc_core::SC_ZERO_TIME);
        for (auto& reset : m_reset) {
            reset.write(false);
        }
        while (total_writes() != m_writes.size() * 2) {
            wait(m_cpu_write);
        }
        for (auto writes : m_writes) {
            TEST_ASSERT(writes == 2);
        }

        m_keepalive.async_detach_suspending();
        sc_core::sc_stop();
        sc_core::sc_suspendable();
    }

    void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>&) override {}

    void mmio_write(int, uint64_t addr, uint64_t data, size_t) override
    {
        const unsigned int cpuid = addr >> 3;

        TEST_ASSERT(cpuid < m_writes.size());
        TEST_ASSERT(data == 1);
        TEST_ASSERT(m_writes[cpuid] < 2);
        ++m_writes[cpuid];
        m_cpu_write.async_notify();
    }

    uint64_t mmio_read(int, uint64_t, size_t) override
    {
        TEST_FAIL("Unexpected CPU read");
    }

    bool dmi_request(int, uint64_t, size_t, tlm::tlm_dmi&) override
    {
        TEST_FAIL("Unexpected DMI request");
    }

private:
    unsigned int total_writes() const
    {
        unsigned int total = 0;
        for (auto writes : m_writes) {
            total += writes;
        }
        return total;
    }
};

constexpr const char* CpuArmStartInResetReleaseTest::FIRMWARE;

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmStartInResetReleaseTest>(argc, argv); }
