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

/*
 * ARM Cortex-A53 shared-memory DMI byte-store test.
 *
 * This mirrors the RSE VM setup more closely than CpuTesterDmi: the DMI target
 * is a gs_memory instance backed by POSIX shared memory, so QBox passes an fd
 * to libqemu when installing the QEMU DMI alias.
 */
class TestMemory : public gs::gs_memory<>
{
public:
    using gs::gs_memory<>::gs_memory;

    bool read_bytes(uint8_t* data, uint64_t offset, uint64_t len) { return read(data, offset, len); }
};

class CpuArmCortexA53ShmemDmiByteStoreTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>
{
    static constexpr uint64_t DMI_ADDR = 0x90000000;
    static constexpr size_t DMI_SIZE = 4096;

    cci::cci_param<bool> p_enable_dmi;
    TestMemory m_dmi_mem;
    bool m_done = false;
    uint64_t m_error = 0;

public:
    static constexpr std::array<uint8_t, 24> PATTERN = {
        0x67, 0xa4, 0x79, 0x10, 0x4f, 0x5d, 0xf3, 0x0a, 0xc8, 0x9e, 0x37, 0x42,
        0x19, 0x80, 0xae, 0x52, 0x01, 0x23, 0x45, 0x89, 0xab, 0xcd, 0xef, 0x5a,
    };

    static constexpr const char* FIRMWARE = R"(
        _start:
            ldr x1, =0x%08)" PRIx64 R"(
            ldr x2, =0x%08)" PRIx64 R"(

            /* First access asks for and installs a DMI mapping. */
            ldrb w0, [x1]

            mov w3, #0x67
            strb w3, [x1, #0]
            mov w3, #0xa4
            strb w3, [x1, #1]
            mov w3, #0x79
            strb w3, [x1, #2]
            mov w3, #0x10
            strb w3, [x1, #3]
            mov w3, #0x4f
            strb w3, [x1, #4]
            mov w3, #0x5d
            strb w3, [x1, #5]
            mov w3, #0xf3
            strb w3, [x1, #6]
            mov w3, #0x0a
            strb w3, [x1, #7]
            mov w3, #0xc8
            strb w3, [x1, #8]
            mov w3, #0x9e
            strb w3, [x1, #9]
            mov w3, #0x37
            strb w3, [x1, #10]
            mov w3, #0x42
            strb w3, [x1, #11]
            mov w3, #0x19
            strb w3, [x1, #12]
            mov w3, #0x80
            strb w3, [x1, #13]
            mov w3, #0xae
            strb w3, [x1, #14]
            mov w3, #0x52
            strb w3, [x1, #15]
            mov w3, #0x01
            strb w3, [x1, #16]
            mov w3, #0x23
            strb w3, [x1, #17]
            mov w3, #0x45
            strb w3, [x1, #18]
            mov w3, #0x89
            strb w3, [x1, #19]
            mov w3, #0xab
            strb w3, [x1, #20]
            mov w3, #0xcd
            strb w3, [x1, #21]
            mov w3, #0xef
            strb w3, [x1, #22]
            mov w3, #0x5a
            strb w3, [x1, #23]

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
            add x4, x4, #1
            ldrb w5, [x1, #16]
            cmp w5, #0x01
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #17]
            cmp w5, #0x23
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #18]
            cmp w5, #0x45
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #19]
            cmp w5, #0x89
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #20]
            cmp w5, #0xab
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #21]
            cmp w5, #0xcd
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #22]
            cmp w5, #0xef
            b.ne fail
            add x4, x4, #1
            ldrb w5, [x1, #23]
            cmp w5, #0x5a
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
            b fail
    )";

    CpuArmCortexA53ShmemDmiByteStoreTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>(n)
        , p_enable_dmi("enable_dmi", true, "Enable DMI for the shared-memory target")
        , m_dmi_mem("dmi_mem", DMI_SIZE)
    {
        char buf[16384];

        m_dmi_mem.p_shmem = true;
        m_dmi_mem.p_shmem_prefix = "qbox_dmi_byte_store_";
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

        TEST_ASSERT(m_done);
        if (m_error != 0) {
            std::cerr << "shared-memory DMI byte-store firmware error: check_index=" << std::dec << (m_error >> 8)
                      << " actual=0x" << std::hex << (m_error & 0xff) << std::dec << std::endl;
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

constexpr std::array<uint8_t, 24> CpuArmCortexA53ShmemDmiByteStoreTest::PATTERN;
constexpr const char* CpuArmCortexA53ShmemDmiByteStoreTest::FIRMWARE;

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53ShmemDmiByteStoreTest>(argc, argv); }
