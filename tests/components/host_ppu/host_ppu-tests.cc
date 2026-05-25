/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <host_ppu.h>

namespace {

constexpr uint64_t PPU_PWPR = 0x000;
constexpr uint64_t PPU_PMER = 0x004;
constexpr uint64_t PPU_PWSR = 0x008;

uint32_t access32(host_ppu& dut, uint64_t offset, tlm::tlm_command command,
                  uint32_t value = 0)
{
    tlm::tlm_generic_payload trans;
    auto data = value;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    dut.b_transport(trans, delay);

    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint32_t read32(host_ppu& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(host_ppu& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(HostPpuTest, PowerPolicyWriteUpdatesPowerStatus)
{
    host_ppu dut("host_ppu");

    write32(dut, PPU_PWPR, 0x00000008u);
    EXPECT_EQ(read32(dut, PPU_PWSR) & 0xfu, 0x8u);

    write32(dut, PPU_PWPR, 0x00000005u);
    EXPECT_EQ(read32(dut, PPU_PWSR) & 0xfu, 0x5u);

    write32(dut, PPU_PWPR, 0x00000000u);
    EXPECT_EQ(read32(dut, PPU_PWSR) & 0xfu, 0x0u);
}

TEST(HostPpuTest, EmulatorRegisterIsWritable)
{
    host_ppu dut("host_ppu_pmer");

    write32(dut, PPU_PMER, 0x00000001u);
    EXPECT_EQ(read32(dut, PPU_PMER), 0x00000001u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
