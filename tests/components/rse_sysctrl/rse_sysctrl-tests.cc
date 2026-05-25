/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <rse_sysctrl.h>

namespace {

constexpr uint64_t SECDBGSTAT = 0x000;
constexpr uint64_t SECDBGSET = 0x004;
constexpr uint64_t SECDBGCLR = 0x008;
constexpr uint64_t RESET_SYNDROME = 0x100;
constexpr uint64_t RESET_MASK = 0x104;
constexpr uint64_t SWRESET = 0x108;
constexpr uint64_t CPUWAIT = 0x120;
constexpr uint64_t DMA_BOOT_EN = 0x254;
constexpr uint64_t DMA_BOOT_ADDR = 0x258;
constexpr uint64_t LCM_DCU_FORCE_DIS = 0x25c;

uint32_t access32(rse_sysctrl& dut, uint64_t offset, tlm::tlm_command command,
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

uint32_t read32(rse_sysctrl& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(rse_sysctrl& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(RseSysctrlTest, ResetValuesMatchFvpRseBootConfiguration)
{
    rse_sysctrl dut("rse_sysctrl");

    EXPECT_EQ(read32(dut, RESET_SYNDROME), 0x80000000u);
    EXPECT_EQ(read32(dut, CPUWAIT), 0x0000000fu);
    EXPECT_EQ(read32(dut, DMA_BOOT_EN), 0x00000001u);
    EXPECT_EQ(read32(dut, DMA_BOOT_ADDR), 0x00000000u);
}

TEST(RseSysctrlTest, ResetDefaultsCanBeOverriddenWithCciParameters)
{
    auto broker = cci::cci_get_global_broker(
        cci::cci_originator("rse_sysctrl_test"));
    broker.set_preset_cci_value("rse_sysctrl_custom.reset_syndrome",
                                cci::cci_value(0x12345678u));
    broker.set_preset_cci_value("rse_sysctrl_custom.cpuwait",
                                cci::cci_value(0x00000003u));
    broker.set_preset_cci_value("rse_sysctrl_custom.dma_boot_en",
                                cci::cci_value(0x00000000u));
    broker.set_preset_cci_value("rse_sysctrl_custom.dma_boot_addr",
                                cci::cci_value(0x11004000u));

    rse_sysctrl dut("rse_sysctrl_custom");

    EXPECT_EQ(read32(dut, RESET_SYNDROME), 0x12345678u);
    EXPECT_EQ(read32(dut, CPUWAIT), 0x00000003u);
    EXPECT_EQ(read32(dut, DMA_BOOT_EN), 0x00000000u);
    EXPECT_EQ(read32(dut, DMA_BOOT_ADDR), 0x11004000u);
}

TEST(RseSysctrlTest, TouchedRegistersAreReadWrite)
{
    rse_sysctrl dut("rse_sysctrl");

    write32(dut, RESET_MASK, 1u << 8);
    EXPECT_EQ(read32(dut, RESET_MASK), 1u << 8);

    write32(dut, CPUWAIT, 0x0000000eu);
    EXPECT_EQ(read32(dut, CPUWAIT), 0x0000000eu);

    write32(dut, DMA_BOOT_EN, 0x00000000u);
    EXPECT_EQ(read32(dut, DMA_BOOT_EN), 0x00000000u);

    write32(dut, DMA_BOOT_ADDR, 0x11000001u);
    EXPECT_EQ(read32(dut, DMA_BOOT_ADDR), 0x11000001u);

    write32(dut, LCM_DCU_FORCE_DIS, 0xaaaaaaaau);
    EXPECT_EQ(read32(dut, LCM_DCU_FORCE_DIS), 0xaaaaaaaau);
}

TEST(RseSysctrlTest, SecureDebugSetAndClearUpdateStatus)
{
    rse_sysctrl dut("rse_sysctrl");

    write32(dut, SECDBGSET, 0x5u);
    EXPECT_EQ(read32(dut, SECDBGSTAT), 0x5u);

    write32(dut, SECDBGCLR, 0x1u);
    EXPECT_EQ(read32(dut, SECDBGSTAT), 0x4u);
}

TEST(RseSysctrlTest, SoftwareResetWriteDoesNotStopSimulation)
{
    rse_sysctrl dut("rse_sysctrl");

    write32(dut, SWRESET, 0xffffffffu);
    EXPECT_EQ(read32(dut, SWRESET), 0x00000000u);
}

TEST(RseSysctrlTest, RejectsUnsupportedAndOutOfRangeTransactions)
{
    rse_sysctrl dut("rse_sysctrl");
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
    trans.set_address(0x1000 - 1);
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
