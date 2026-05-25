/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <rse_integrity_checker.h>

namespace {

constexpr uint64_t ICBC = 0x000;
constexpr uint64_t ICC = 0x004;
constexpr uint64_t ICIS = 0x008;
constexpr uint64_t ICIE = 0x00c;
constexpr uint64_t ICIC = 0x014;
constexpr uint64_t ICDA = 0x018;
constexpr uint64_t ICDL = 0x01c;
constexpr uint64_t ICEVA = 0x020;
constexpr uint64_t PIDR0 = 0xfe0;
constexpr uint32_t ICIS_DONE = 1u << 0;

uint32_t access32(rse_integrity_checker& dut, uint64_t offset,
                  tlm::tlm_command command, uint32_t value = 0)
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

uint32_t read32(rse_integrity_checker& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(rse_integrity_checker& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(RseIntegrityCheckerTest, ResetValuesMatchRseTfMDriverExpectations)
{
    rse_integrity_checker dut("rse_integrity_checker");

    EXPECT_EQ(read32(dut, ICBC), 0x00000109u);
    EXPECT_EQ(read32(dut, ICIS), 0x00000000u);
    EXPECT_EQ(read32(dut, PIDR0), 0x000000fdu);
}

TEST(RseIntegrityCheckerTest, ProgrammingRegistersAreWritable)
{
    rse_integrity_checker dut("rse_integrity_checker_regs");

    write32(dut, ICIE, 0xffu);
    write32(dut, ICDA, 0x34000000u);
    write32(dut, ICDL, 0x20u);
    write32(dut, ICEVA, 0x30001000u);

    EXPECT_EQ(read32(dut, ICIE), 0xffu);
    EXPECT_EQ(read32(dut, ICDA), 0x34000000u);
    EXPECT_EQ(read32(dut, ICDL), 0x20u);
    EXPECT_EQ(read32(dut, ICEVA), 0x30001000u);
}

TEST(RseIntegrityCheckerTest, StartSetsCompletionAndClearRemovesIt)
{
    rse_integrity_checker dut("rse_integrity_checker_start");

    write32(dut, ICC, 0x00000001u);
    EXPECT_EQ(read32(dut, ICIS) & ICIS_DONE, ICIS_DONE);

    write32(dut, ICIC, 0xffffffffu);
    EXPECT_EQ(read32(dut, ICIS), 0u);
}

TEST(RseIntegrityCheckerTest, ReadOnlyRegistersIgnoreWrites)
{
    rse_integrity_checker dut("rse_integrity_checker_ro");

    write32(dut, ICBC, 0xffffffffu);
    write32(dut, ICIS, 0xffffffffu);

    EXPECT_EQ(read32(dut, ICBC), 0x00000109u);
    EXPECT_EQ(read32(dut, ICIS), 0x00000000u);
}

TEST(RseIntegrityCheckerTest, RejectsOutOfRangeTransactions)
{
    rse_integrity_checker dut("rse_integrity_checker_bounds");
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
