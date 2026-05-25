/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <iostream>

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "cortex-a53.h"
#include "gs_memory.h"
#include "qemu-instance.h"

class ExternalWriteMemory : public gs::gs_memory<>
{
public:
    using gs::gs_memory<>::gs_memory;

    bool read_bytes(uint8_t* data, uint64_t offset, uint64_t len) { return read(data, offset, len); }
    bool write_bytes(const uint8_t* data, uint64_t offset, uint64_t len) { return write(data, offset, len); }
};

/*
 * ARM Cortex-A53 shared-memory DMI external-write visibility test.
 *
 * The CPU first touches the memory so QEMU installs a DMI mapping. The test
 * bench then writes the backing gs_memory directly, modeling a SystemC
 * peripheral/DMA write, and the CPU must observe those bytes through its DMI
 * mapping.
 */
class CpuArmCortexA53ShmemDmiExternalWriteTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>
{
    static constexpr uint64_t DMI_ADDR = 0x90000000;
    static constexpr size_t DMI_SIZE = 4096;
    static constexpr uint64_t READY_MAGIC = 0xfeed;

    cci::cci_param<bool> p_enable_dmi;
    ExternalWriteMemory m_dmi_mem;
    bool m_ready = false;
    bool m_done = false;
    uint64_t m_error = 0;

public:
    static constexpr std::array<uint8_t, 16> PATTERN = {
        0x67, 0xa4, 0x79, 0x10, 0x4f, 0x5d, 0xf3, 0x0a,
        0xc8, 0x9e, 0x37, 0x42, 0x19, 0x80, 0xae, 0x52,
    };

    static constexpr const char* FIRMWARE = R"(
        _start:
            ldr x1, =0x%08)" PRIx64 R"(
            ldr x2, =0x%08)" PRIx64 R"(

            /* First access asks for and installs a DMI mapping. */
            ldrb w0, [x1]

            mov x3, #0xfeed
            str x3, [x2]

            mov x6, #0x100000
        wait_external_write:
            ldrb w5, [x1, #0]
            cmp w5, #0x67
            b.eq verify
            subs x6, x6, #1
            b.ne wait_external_write

            mov x0, #0xbeef
            str x0, [x2]
            b fail_loop

        verify:
            mov x4, #0

            ldrb w5, [x1, #0]
            cmp w5, #0x67
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #1]
            cmp w5, #0xa4
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #2]
            cmp w5, #0x79
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #3]
            cmp w5, #0x10
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #4]
            cmp w5, #0x4f
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #5]
            cmp w5, #0x5d
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #6]
            cmp w5, #0xf3
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #7]
            cmp w5, #0x0a
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #8]
            cmp w5, #0xc8
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #9]
            cmp w5, #0x9e
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #10]
            cmp w5, #0x37
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #11]
            cmp w5, #0x42
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #12]
            cmp w5, #0x19
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #13]
            cmp w5, #0x80
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #14]
            cmp w5, #0xae
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #15]
            cmp w5, #0x52
            b.ne fail

        success:
            mov x0, #0
            str x0, [x2]
            wfi
            b success

        fail:
            add x0, x4, #1
            lsl x0, x0, #8
            orr x0, x0, x5
            str x0, [x2]
        fail_loop:
            b fail_loop
    )";

    CpuArmCortexA53ShmemDmiExternalWriteTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>(n)
        , p_enable_dmi("enable_dmi", true, "Enable DMI for the shared-memory target")
        , m_dmi_mem("dmi_mem", DMI_SIZE)
    {
        char buf[16384];

        m_dmi_mem.p_shmem = true;
        m_dmi_mem.p_shmem_prefix = "qbox_dmi_external_write_";
        m_dmi_mem.p_init_mem = true;
        m_dmi_mem.p_init_mem_val = 0;
        m_dmi_mem.p_dmi = p_enable_dmi.get_value();
        m_router.add_target(m_dmi_mem.socket, DMI_ADDR, DMI_SIZE);

        std::snprintf(buf, sizeof(buf), FIRMWARE, DMI_ADDR, CpuTesterMmio::MMIO_ADDR);
        set_firmware(buf);
    }

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        TEST_ASSERT(id == CpuTesterMmio::SOCKET_MMIO);
        TEST_ASSERT(addr == 0);

        if (data == READY_MAGIC) {
            m_ready = true;
            TEST_ASSERT(m_dmi_mem.write_bytes(PATTERN.data(), 0, PATTERN.size()));
            return;
        }

        m_error = data;
        m_done = true;
        sc_core::sc_stop();
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override
    {
        TEST_FAIL("Unexpected MMIO read");
        return 0;
    }

    virtual void end_of_simulation() override
    {
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>::end_of_simulation();

        std::array<uint8_t, PATTERN.size()> observed = {};
        TEST_ASSERT(m_dmi_mem.read_bytes(observed.data(), 0, observed.size()));

        TEST_ASSERT(m_ready);
        TEST_ASSERT(m_done);
        if (m_error != 0) {
            std::cerr << "shared-memory DMI external-write firmware error: check_index=" << std::dec
                      << (m_error >> 8) << " actual=0x" << std::hex << (m_error & 0xff) << std::dec << std::endl;
            std::cerr << "shared-memory DMI buffer first bytes:";
            for (uint8_t byte : observed) {
                std::cerr << " 0x" << std::hex << static_cast<unsigned>(byte);
            }
            std::cerr << std::dec << std::endl;
        }
        TEST_ASSERT(m_error == 0);
        for (size_t i = 0; i < PATTERN.size(); ++i) {
            TEST_ASSERT(observed[i] == PATTERN[i]);
        }
    }
};

constexpr std::array<uint8_t, 16> CpuArmCortexA53ShmemDmiExternalWriteTest::PATTERN;
constexpr const char* CpuArmCortexA53ShmemDmiExternalWriteTest::FIRMWARE;

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53ShmemDmiExternalWriteTest>(argc, argv); }
