/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <rse_sam.h>

namespace {

constexpr uint64_t SAMBC = 0x000;
constexpr uint64_t SAMES0 = 0x004;
constexpr uint64_t SAMECL0 = 0x00c;
constexpr uint64_t SAMEM0 = 0x014;
constexpr uint64_t SAMIM0 = 0x01c;
constexpr uint64_t SAMRRLS0 = 0x024;
constexpr uint64_t SAMEC0 = 0x044;
constexpr uint64_t SAMWDCIV = 0x068;
constexpr uint64_t SAMICV = 0x070;
constexpr uint64_t VMPWCA0 = 0x084;
constexpr uint64_t PIDR0 = 0xfe0;

uint32_t access32(rse_sam& dut, uint64_t offset, tlm::tlm_command command,
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

uint32_t read32(rse_sam& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(rse_sam& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(RseSamTest, ResetValuesMatchRseTfMDriverExpectations)
{
    rse_sam dut("rse_sam");

    EXPECT_EQ(read32(dut, SAMBC), 0x00000700u);
    EXPECT_EQ(read32(dut, SAMES0), 0x00000000u);
    EXPECT_EQ(read32(dut, PIDR0), 0x000000f4u);
}

TEST(RseSamTest, ProgrammingRegistersAreWritable)
{
    rse_sam dut("rse_sam_regs");

    write32(dut, SAMEM0, 0x00000001u);
    write32(dut, SAMIM0, 0x00000002u);
    write32(dut, SAMRRLS0, 0x76543210u);
    write32(dut, SAMEC0, 0x00000003u);
    write32(dut, SAMWDCIV, 0x000000ffu);
    write32(dut, SAMICV, 0x00000123u);
    write32(dut, VMPWCA0, 0x00001000u);

    EXPECT_EQ(read32(dut, SAMEM0), 0x00000001u);
    EXPECT_EQ(read32(dut, SAMIM0), 0x00000002u);
    EXPECT_EQ(read32(dut, SAMRRLS0), 0x76543210u);
    EXPECT_EQ(read32(dut, SAMEC0), 0x00000003u);
    EXPECT_EQ(read32(dut, SAMWDCIV), 0x000000ffu);
    EXPECT_EQ(read32(dut, SAMICV), 0x00000123u);
    EXPECT_EQ(read32(dut, VMPWCA0), 0x00001000u);
}

TEST(RseSamTest, ReadOnlyRegistersIgnoreWrites)
{
    rse_sam dut("rse_sam_ro");

    write32(dut, SAMBC, 0xffffffffu);
    write32(dut, SAMES0, 0xffffffffu);

    EXPECT_EQ(read32(dut, SAMBC), 0x00000700u);
    EXPECT_EQ(read32(dut, SAMES0), 0x00000000u);
}

TEST(RseSamTest, EventClearRegisterIsWritable)
{
    rse_sam dut("rse_sam_clear");

    write32(dut, SAMECL0, 0xffffffffu);
    EXPECT_EQ(read32(dut, SAMECL0), 0xffffffffu);
    EXPECT_EQ(read32(dut, SAMES0), 0x00000000u);
}

TEST(RseSamTest, RejectsOutOfRangeTransactions)
{
    rse_sam dut("rse_sam_bounds");
    tlm::tlm_generic_payload trans;
    uint32_t data = 0;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x1000 - 1);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    trans.set_data_length(sizeof(data));
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
