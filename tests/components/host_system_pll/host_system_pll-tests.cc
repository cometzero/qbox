/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <host_system_pll.h>
#include <systemc>
#include <tlm>

namespace {

uint32_t access32(host_system_pll& dut, uint64_t offset,
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

uint32_t read32(host_system_pll& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(host_system_pll& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(HostSystemPllTest, WriteImmediatelyReportsLocked)
{
    host_system_pll dut("host_system_pll");

    write32(dut, 0x04, 0x00000298u);

    EXPECT_EQ(read32(dut, 0x04), 0x00000299u);
}

TEST(HostSystemPllTest, LockMaskIsConfigurable)
{
    host_system_pll dut("host_system_pll_lock_mask");
    dut.p_lock_mask = 0x00000080u;

    write32(dut, 0x10, 0x00000100u);

    EXPECT_EQ(read32(dut, 0x10), 0x00000180u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
