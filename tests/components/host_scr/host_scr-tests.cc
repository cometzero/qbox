/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <host_scr.h>
#include <systemc>
#include <tlm>

namespace {

constexpr uint64_t SID_SYSTEM_CFG = 0x070;
constexpr uint64_t CPUHALT = 0x300;
constexpr uint64_t MEMPROTCTLR = 0x500;
constexpr uint64_t SAFECTLR = 0x600;

uint32_t access32(host_scr& dut, uint64_t offset, tlm::tlm_command command,
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

uint32_t read32(host_scr& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(host_scr& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(HostScrTest, Cl1PresentResetValueIsConfigurable)
{
    host_scr dut("host_scr_cl1");
    dut.p_cl1_present = true;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, SID_SYSTEM_CFG) & 0x1u, 0x1u);
}

TEST(HostScrTest, WritableControlRegistersPreserveWrites)
{
    host_scr dut("host_scr_writes");
    dut.p_cl1_present = true;
    dut.before_end_of_elaboration();

    write32(dut, CPUHALT, 0x00000f01u);
    write32(dut, MEMPROTCTLR, 0x00000033u);
    write32(dut, SAFECTLR, 0x00000001u);

    EXPECT_EQ(read32(dut, CPUHALT), 0x00000f01u);
    EXPECT_EQ(read32(dut, MEMPROTCTLR), 0x00000033u);
    EXPECT_EQ(read32(dut, SAFECTLR), 0x00000001u);
}

TEST(HostScrTest, SystemConfigIsReadOnlyToFirmwareWrites)
{
    host_scr dut("host_scr_readonly");
    dut.p_cl1_present = true;
    dut.before_end_of_elaboration();

    write32(dut, SID_SYSTEM_CFG, 0);
    EXPECT_EQ(read32(dut, SID_SYSTEM_CFG) & 0x1u, 0x1u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
