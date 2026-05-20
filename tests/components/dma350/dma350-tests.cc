/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <dma350.h>

namespace {

constexpr uint64_t DMASECINFO = 0xfb0;
constexpr uint64_t CH0_CMD = 0x1000;
constexpr uint64_t CH0_CTRL = 0x100c;
constexpr uint64_t CH0_DESTRANSCFG = 0x102c;

uint32_t access32(dma350& dut, uint64_t offset, tlm::tlm_command command, uint32_t value = 0)
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

uint32_t read32(dma350& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(dma350& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(Dma350Test, ResetValuesCoverEarlyBl1Polling)
{
    dma350 dut("dma350");

    EXPECT_EQ(read32(dut, DMASECINFO), 0x0u);
    EXPECT_EQ(read32(dut, CH0_CTRL), 0x00200200u);
    EXPECT_EQ(read32(dut, CH0_DESTRANSCFG), 0x000f0400u);
}

TEST(Dma350Test, ChannelCommandCompletesImmediately)
{
    dma350 dut("dma350");

    write32(dut, CH0_CMD, 0xffffffffu);
    EXPECT_EQ(read32(dut, CH0_CMD), 0x0u);
}

TEST(Dma350Test, RejectsUnsupportedAndOutOfRangeTransactions)
{
    dma350 dut("dma350");
    tlm::tlm_generic_payload trans;
    uint8_t data[3] = {};
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0);
    trans.set_data_ptr(data);
    trans.set_data_length(sizeof(data));
    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_ADDRESS_ERROR_RESPONSE);

    trans.set_data_length(sizeof(uint32_t));
    trans.set_address(0x2000 - 1);
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
