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

constexpr uint64_t CL0_CONFIG_0 = 0x000;
constexpr uint64_t CL0_CONFIG_1 = 0x004;
constexpr uint64_t CL0_CONFIG_2 = 0x008;
constexpr uint64_t CL0_C0_CONFIG_0 = 0x010;
constexpr uint64_t CL0_C0_CONFIG_1 = 0x014;
constexpr uint64_t CL0_C0_CONFIG_2 = 0x018;
constexpr uint64_t CL0_C0_CONFIG_3 = 0x01c;
constexpr uint64_t SID_SYSTEM_ID = 0x040;
constexpr uint64_t SID_SOC_ID = 0x050;
constexpr uint64_t SID_CHIP_ID = 0x060;
constexpr uint64_t SID_SYSTEM_CFG = 0x070;
constexpr uint64_t CPUHALT = 0x300;
constexpr uint64_t MEMPROTCTLR = 0x500;
constexpr uint64_t SAFECTLR = 0x600;
constexpr uint64_t SID_PIDR4 = 0xfd0;
constexpr uint64_t SID_PIDR0 = 0xfe0;
constexpr uint64_t SID_PIDR1 = 0xfe4;
constexpr uint64_t SID_PIDR2 = 0xfe8;
constexpr uint64_t SID_PIDR3 = 0xfec;
constexpr uint64_t SID_CIDR0 = 0xff0;
constexpr uint64_t SID_CIDR1 = 0xff4;
constexpr uint64_t SID_CIDR2 = 0xff8;
constexpr uint64_t SID_CIDR3 = 0xffc;

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

tlm::tlm_response_status access32_status(host_scr& dut, uint64_t offset,
                                          tlm::tlm_command command,
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

    return trans.get_response_status();
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

TEST(HostScrTest, Cl0ResetConfigValuesAreConfigurable)
{
    host_scr dut("host_scr_cl0_config");
    dut.p_cl0_config_0 = 0x01201717u;
    dut.p_cl0_config_1 = 0x01100002u;
    dut.p_cl0_config_2 = 0x00000034u;
    dut.p_cl0_c0_config_0 = 0x01000001u;
    dut.p_cl0_c0_config_1 = 0x01001000u;
    dut.p_cl0_c0_config_2 = 0x01200000u;
    dut.p_cl0_c0_config_3 = 0x00000000u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, CL0_CONFIG_0), 0x01201717u);
    EXPECT_EQ(read32(dut, CL0_CONFIG_1), 0x01100002u);
    EXPECT_EQ(read32(dut, CL0_CONFIG_2), 0x00000034u);
    EXPECT_EQ(read32(dut, CL0_C0_CONFIG_0), 0x01000001u);
    EXPECT_EQ(read32(dut, CL0_C0_CONFIG_1), 0x01001000u);
    EXPECT_EQ(read32(dut, CL0_C0_CONFIG_2), 0x01200000u);
    EXPECT_EQ(read32(dut, CL0_C0_CONFIG_3), 0x00000000u);
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

TEST(HostScrTest, PcidResetValuesMatchApolloSid)
{
    host_scr dut("host_scr_pcid");
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, SID_PIDR4), 0x04u);
    EXPECT_EQ(read32(dut, SID_PIDR0), 0x3cu);
    EXPECT_EQ(read32(dut, SID_PIDR1), 0xb7u);
    EXPECT_EQ(read32(dut, SID_PIDR2), 0x0bu);
    EXPECT_EQ(read32(dut, SID_PIDR3), 0x00u);
    EXPECT_EQ(read32(dut, SID_CIDR0), 0x0du);
    EXPECT_EQ(read32(dut, SID_CIDR1), 0xf0u);
    EXPECT_EQ(read32(dut, SID_CIDR2), 0x05u);
    EXPECT_EQ(read32(dut, SID_CIDR3), 0xb1u);
}

TEST(HostScrTest, ApSidResetProfileIsParameterizable)
{
    host_scr dut("host_scr_ap_sid");
    dut.p_system_id = 0x0047773du;
    dut.p_soc_id = 0x00000000u;
    dut.p_chip_id = 0x00000000u;
    dut.p_pidr4 = 0x00000004u;
    dut.p_pidr0 = 0x0000003du;
    dut.p_pidr1 = 0x000000b7u;
    dut.p_pidr2 = 0x0000000bu;
    dut.p_pidr3 = 0x00000000u;
    dut.p_cidr0 = 0x0000000du;
    dut.p_cidr1 = 0x000000f0u;
    dut.p_cidr2 = 0x00000005u;
    dut.p_cidr3 = 0x000000b1u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, SID_SYSTEM_ID), 0x0047773du);
    EXPECT_EQ(read32(dut, SID_SOC_ID), 0x00000000u);
    EXPECT_EQ(read32(dut, SID_CHIP_ID), 0x00000000u);
    EXPECT_EQ(read32(dut, SID_PIDR4), 0x00000004u);
    EXPECT_EQ(read32(dut, SID_PIDR0), 0x0000003du);
    EXPECT_EQ(read32(dut, SID_PIDR1), 0x000000b7u);
    EXPECT_EQ(read32(dut, SID_PIDR2), 0x0000000bu);
    EXPECT_EQ(read32(dut, SID_PIDR3), 0x00000000u);
    EXPECT_EQ(read32(dut, SID_CIDR0), 0x0000000du);
    EXPECT_EQ(read32(dut, SID_CIDR1), 0x000000f0u);
    EXPECT_EQ(read32(dut, SID_CIDR2), 0x00000005u);
    EXPECT_EQ(read32(dut, SID_CIDR3), 0x000000b1u);
}

TEST(HostScrTest, SidIdentityRegistersAreReadOnlyToFirmwareWrites)
{
    host_scr dut("host_scr_sid_readonly");
    dut.p_system_id = 0x0047773du;
    dut.p_pidr0 = 0x0000003du;
    dut.before_end_of_elaboration();

    write32(dut, SID_SYSTEM_ID, 0xffffffffu);
    write32(dut, SID_SOC_ID, 0xffffffffu);
    write32(dut, SID_CHIP_ID, 0xffffffffu);
    write32(dut, SID_PIDR0, 0xffffffffu);

    EXPECT_EQ(read32(dut, SID_SYSTEM_ID), 0x0047773du);
    EXPECT_EQ(read32(dut, SID_SOC_ID), 0x00000000u);
    EXPECT_EQ(read32(dut, SID_CHIP_ID), 0x00000000u);
    EXPECT_EQ(read32(dut, SID_PIDR0), 0x0000003du);
}

TEST(HostScrTest, OutOfWindowAccessReturnsAddressError)
{
    host_scr dut("host_scr_bad_offset");
    dut.before_end_of_elaboration();

    EXPECT_EQ(access32_status(dut, 0x10000, tlm::TLM_READ_COMMAND),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(access32_status(dut, 0x10000, tlm::TLM_WRITE_COMMAND, 0),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
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
