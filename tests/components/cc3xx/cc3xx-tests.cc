/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <cc3xx.h>

namespace {

constexpr uint64_t RNG_ISR = 0x104;
constexpr uint64_t RNG_ICR = 0x108;
constexpr uint64_t EHR_DATA = 0x114;
constexpr uint64_t RND_SOURCE_ENABLE = 0x12c;
constexpr uint64_t SAMPLE_CNT1 = 0x130;
constexpr uint64_t RNG_SW_RESET = 0x140;
constexpr uint64_t RNG_CLK_ENABLE = 0x1c4;
constexpr uint64_t AES_HW_FLAGS = 0x4c8;
constexpr uint64_t AES_RBG_SEEDING_RDY = 0x4fc;
constexpr uint64_t HOST_BOOT = 0xa28;
constexpr uint64_t HOST_CC_IS_IDLE = 0xa7c;
constexpr uint64_t HOST_SF_READY = 0xa90;
constexpr uint64_t LCS_REG = 0x1f14;

uint32_t access32(cc3xx& dut, uint64_t offset, tlm::tlm_command command, uint32_t value = 0)
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

uint32_t read32(cc3xx& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(cc3xx& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(Cc3xxTest, ResetValuesCoverEarlyTfmReadiness)
{
    cc3xx dut("cc3xx");

    EXPECT_EQ(read32(dut, RNG_ISR), 0x1u);
    EXPECT_EQ(read32(dut, SAMPLE_CNT1), 0xffffu);
    EXPECT_EQ(read32(dut, AES_RBG_SEEDING_RDY), 0x1u);
    EXPECT_NE(read32(dut, AES_HW_FLAGS) & (1u << 3), 0u);
    EXPECT_NE(read32(dut, AES_HW_FLAGS) & (1u << 12), 0u);
    EXPECT_NE(read32(dut, HOST_BOOT) & (1u << 11), 0u);
    EXPECT_EQ(read32(dut, HOST_CC_IS_IDLE), 0x1u);
    EXPECT_EQ(read32(dut, HOST_SF_READY), 0x1u);
    EXPECT_EQ(read32(dut, LCS_REG), 0x5u);
}

TEST(Cc3xxTest, RngControlRegistersSupportObservedBl1Sequence)
{
    cc3xx dut("cc3xx");

    write32(dut, RNG_CLK_ENABLE, 0x1);
    EXPECT_EQ(read32(dut, RNG_CLK_ENABLE), 0x1u);

    write32(dut, RNG_SW_RESET, 0x1);
    EXPECT_EQ(read32(dut, RNG_SW_RESET), 0x1u);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x0u);

    write32(dut, RND_SOURCE_ENABLE, 0x1);
    EXPECT_EQ(read32(dut, RND_SOURCE_ENABLE), 0x1u);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x1u);

    write32(dut, RNG_ICR, 0x1);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x0u);
}

TEST(Cc3xxTest, DeterministicEntropyWordsAreReadable)
{
    cc3xx dut("cc3xx");

    EXPECT_EQ(read32(dut, EHR_DATA + 0x00), 0x243f6a88u);
    EXPECT_EQ(read32(dut, EHR_DATA + 0x04), 0x85a308d3u);
    EXPECT_EQ(read32(dut, EHR_DATA + 0x08), 0x13198a2eu);
    EXPECT_EQ(read32(dut, RNG_ISR), 0x1u);
}

TEST(Cc3xxTest, RejectsUnsupportedAndOutOfRangeTransactions)
{
    cc3xx dut("cc3xx");
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
    trans.set_address(0x10000 - 1);
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
