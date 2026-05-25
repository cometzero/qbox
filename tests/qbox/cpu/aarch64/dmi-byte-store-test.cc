/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdint>
#include <iostream>

#include "test/cpu.h"
#include "test/tester/dmi.h"

#include "cortex-a53.h"
#include "qemu-instance.h"

/*
 * ARM Cortex-A53 DMI byte-store test.
 *
 * The RSE boot path copies encrypted image metadata a byte at a time into
 * VM memory. Prime a DMI mapping, then use strb/ldrb so the QEMU DMI alias
 * must preserve every byte after the first regular TLM access.
 */
class CpuArmCortexA53DmiByteStoreTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>
{
    cci::cci_param<bool> p_enable_dmi;
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

    CpuArmCortexA53DmiByteStoreTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>(n)
        , p_enable_dmi("enable_dmi", true, "Enable DMI for the byte-store target")
    {
        char buf[16384];

        std::snprintf(buf, sizeof(buf), FIRMWARE, CpuTesterDmi::DMI_ADDR, CpuTesterDmi::MMIO_ADDR);
        set_firmware(buf);
    }

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        if (id == CpuTesterDmi::SOCKET_DMI) {
            return;
        }

        TEST_ASSERT(id == CpuTesterDmi::SOCKET_MMIO);
        TEST_ASSERT(addr == 0);

        m_error = data;
        m_done = true;
        sc_core::sc_stop();
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override
    {
        if (id == CpuTesterDmi::SOCKET_DMI) {
            return 0;
        }

        TEST_FAIL("Unexpected MMIO read");
        return 0;
    }

    virtual bool dmi_request(int id, uint64_t addr, size_t len, tlm::tlm_dmi& ret) override
    {
        TEST_ASSERT(id == CpuTesterDmi::SOCKET_DMI);
        if (!p_enable_dmi) {
            return false;
        }
        ret.set_start_address(0);
        ret.set_end_address(CpuTesterDmi::DMI_SIZE - 1);
        return true;
    }

    virtual void end_of_simulation() override
    {
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>::end_of_simulation();

        TEST_ASSERT(m_done);
        if (m_error != 0) {
            std::cerr << "DMI byte-store firmware error: check_index=" << std::dec << (m_error >> 8)
                      << " actual=0x" << std::hex << (m_error & 0xff) << std::dec << std::endl;
            std::cerr << "DMI buffer first bytes:";
            for (size_t i = 0; i < PATTERN.size(); ++i) {
                std::cerr << " 0x" << std::hex << static_cast<unsigned>(m_tester.get_buf_byte(i));
            }
            std::cerr << std::dec << std::endl;
        }
        TEST_ASSERT(m_error == 0);
        for (size_t i = 0; i < PATTERN.size(); ++i) {
            TEST_ASSERT(m_tester.get_buf_byte(i) == PATTERN[i]);
        }
    }
};

constexpr std::array<uint8_t, 24> CpuArmCortexA53DmiByteStoreTest::PATTERN;
constexpr const char* CpuArmCortexA53DmiByteStoreTest::FIRMWARE;

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53DmiByteStoreTest>(argc, argv); }
