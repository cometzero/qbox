/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <host_smcf_mgi.h>
#include <systemc>
#include <tlm>

namespace {

constexpr uint64_t GRP_ID = 0x000;
constexpr uint64_t DATA_INFO = 0x008;
constexpr uint64_t FEAT0 = 0x010;
constexpr uint64_t MON_REQ = 0x060;
constexpr uint64_t MON_STAT = 0x070;
constexpr uint64_t MODE_REQ0 = 0x090;
constexpr uint64_t MODE_STAT0 = 0x0c0;
constexpr uint64_t IRQ_STAT = 0x100;
constexpr uint64_t SMP_EN = 0x030;
constexpr uint64_t REGION_END = 0xfffc;

uint32_t access32(host_smcf_mgi& dut, uint64_t offset,
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

uint32_t read32(host_smcf_mgi& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(host_smcf_mgi& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(HostSmcfMgiTest, ResetValuesDescribeApolloOneMonitorMgi)
{
    host_smcf_mgi dut("host_smcf_mgi_reset");

    EXPECT_EQ(read32(dut, GRP_ID), 0u);
    EXPECT_EQ(read32(dut, DATA_INFO), 0x07c0000bu);
    EXPECT_EQ(read32(dut, FEAT0), 0x90000000u);
}

TEST(HostSmcfMgiTest, ConfigurableMonitorCountIsEncoded)
{
    host_smcf_mgi dut("host_smcf_mgi_monitors");
    dut.p_group_id = 0x253u;
    dut.p_monitor_count = 4u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, GRP_ID), 0x00030253u);
}

TEST(HostSmcfMgiTest, MonitorRequestImmediatelyUpdatesStatus)
{
    host_smcf_mgi dut("host_smcf_mgi_monitor_status");

    write32(dut, MON_REQ, 0x1u);

    EXPECT_EQ(read32(dut, MON_REQ), 0x1u);
    EXPECT_EQ(read32(dut, MON_STAT), 0x1u);
}

TEST(HostSmcfMgiTest, ModeRequestImmediatelyUpdatesStatus)
{
    host_smcf_mgi dut("host_smcf_mgi_mode_status");

    write32(dut, MODE_REQ0, 0x1u);

    EXPECT_EQ(read32(dut, MODE_STAT0), 0x1u);
}

TEST(HostSmcfMgiTest, SampleEnableDoesNotReportOngoingBusy)
{
    host_smcf_mgi dut("host_smcf_mgi_sample_enable");

    write32(dut, SMP_EN, 0x3u);

    EXPECT_EQ(read32(dut, SMP_EN), 0x1u);
}

TEST(HostSmcfMgiTest, IrqStatusIsWriteOneToClear)
{
    host_smcf_mgi dut("host_smcf_mgi_irq_status");
    uint32_t value = 0x5u;
    tlm::tlm_generic_payload trans;

    trans.set_address(IRQ_STAT);
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_data_length(sizeof(value));
    trans.set_streaming_width(sizeof(value));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    dut.transport_dbg(trans);
    EXPECT_EQ(read32(dut, IRQ_STAT), 0u);
}

TEST(HostSmcfMgiTest, FullMappedRegionIsReadable)
{
    host_smcf_mgi dut("host_smcf_mgi_region");

    EXPECT_EQ(read32(dut, REGION_END), 0u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
