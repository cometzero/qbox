/*
 * This file is part of libqbox
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
 * A read-only DMI grant is only safe if guest writes still reach the target
 * callback. This matches stateful flash devices: array reads can use DMI, but
 * command writes must keep their side effects.
 */
class CpuArmCortexA53DmiReadonlyWriteFallbackTest
    : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>
{
    bool m_done = false;
    uint64_t m_error = 0;
    size_t m_dmi_writes = 0;

public:
    static constexpr std::array<uint8_t, 4> PATTERN = {
        0xa5, 0x5a, 0xc3, 0x3c,
    };

    static constexpr const char* FIRMWARE = R"(
        _start:
            ldr x1, =0x%08)" PRIx64 R"(
            ldr x2, =0x%08)" PRIx64 R"(

            /* Prime the DMI mapping with a read-only grant. */
            ldrb w0, [x1]

            mov w3, #0xa5
            strb w3, [x1, #0]
            mov w3, #0x5a
            strb w3, [x1, #1]
            mov w3, #0xc3
            strb w3, [x1, #2]
            mov w3, #0x3c
            strb w3, [x1, #3]

            ldrb w5, [x1, #0]
            cmp w5, #0xa5
            b.ne fail0
            ldrb w5, [x1, #1]
            cmp w5, #0x5a
            b.ne fail1
            ldrb w5, [x1, #2]
            cmp w5, #0xc3
            b.ne fail2
            ldrb w5, [x1, #3]
            cmp w5, #0x3c
            b.ne fail3

        success:
            mov x0, #0
            str x0, [x2]
            wfi
            b success

        fail0:
            mov x4, #1
            b fail
        fail1:
            mov x4, #2
            b fail
        fail2:
            mov x4, #3
            b fail
        fail3:
            mov x4, #4
            b fail

        fail:
            lsl x0, x4, #8
            orr x0, x0, x5
            str x0, [x2]
            b fail
    )";

    CpuArmCortexA53DmiReadonlyWriteFallbackTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>(n)
    {
        char buf[4096];

        this->m_tester.disable_dmi_write();
        std::snprintf(buf, sizeof(buf), FIRMWARE, CpuTesterDmi::DMI_ADDR, CpuTesterDmi::MMIO_ADDR);
        set_firmware(buf);
    }

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        if (id == CpuTesterDmi::SOCKET_DMI) {
            TEST_ASSERT(addr < PATTERN.size());
            TEST_ASSERT(len == 1);
            TEST_ASSERT(data == PATTERN[addr]);
            ++m_dmi_writes;
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
        ret.set_start_address(0);
        ret.set_end_address(CpuTesterDmi::DMI_SIZE - 1);
        return true;
    }

    virtual void end_of_simulation() override
    {
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>::end_of_simulation();

        TEST_ASSERT(m_done);
        if (m_error != 0) {
            std::cerr << "read-only DMI write fallback error: check_index=" << std::dec << (m_error >> 8)
                      << " actual=0x" << std::hex << (m_error & 0xff) << std::dec << std::endl;
        }
        TEST_ASSERT(m_error == 0);
        TEST_ASSERT(m_dmi_writes == PATTERN.size());
        for (size_t i = 0; i < PATTERN.size(); ++i) {
            TEST_ASSERT(this->m_tester.get_buf_byte(i) == PATTERN[i]);
        }
    }
};

constexpr std::array<uint8_t, 4> CpuArmCortexA53DmiReadonlyWriteFallbackTest::PATTERN;
constexpr const char* CpuArmCortexA53DmiReadonlyWriteFallbackTest::FIRMWARE;

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53DmiReadonlyWriteFallbackTest>(argc, argv); }
