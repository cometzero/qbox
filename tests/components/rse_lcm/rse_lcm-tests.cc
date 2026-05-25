/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <rse_lcm.h>

namespace {

constexpr uint64_t LCS_VALUE = 0x000;
constexpr uint64_t TP_MODE = 0x008;
constexpr uint64_t FATAL_ERR = 0x00c;
constexpr uint64_t SP_ENABLE = 0x014;
constexpr uint64_t OTP_ADDR_WIDTH = 0x018;
constexpr uint64_t OTP_SIZE = 0x01c;
constexpr uint64_t DCU_EN0 = 0x100;
constexpr uint64_t OTP_WINDOW = 0x1000;

uint32_t access32(rse_lcm& dut, uint64_t offset, tlm::tlm_command command,
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

uint32_t read32(rse_lcm& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(rse_lcm& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

uint32_t read_file32(const std::string& path)
{
    uint32_t value = 0;
    std::ifstream file(path, std::ios::binary);

    EXPECT_TRUE(file);
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    EXPECT_TRUE(file);
    return value;
}

} // namespace

TEST(RseLcmTest, ResetValuesExposeProvisionedLifecycle)
{
    rse_lcm dut("rse_lcm");

    EXPECT_EQ(read32(dut, LCS_VALUE), 0xeeeea5a5u);
    EXPECT_EQ(read32(dut, TP_MODE), 0x2222aa55u);
    EXPECT_EQ(read32(dut, SP_ENABLE), 0x00000000u);
    EXPECT_EQ(read32(dut, OTP_ADDR_WIDTH), 0x00000010u);
    EXPECT_EQ(read32(dut, OTP_SIZE), 0x00010000u);
    EXPECT_EQ(read32(dut, DCU_EN0), 0xffffffffu);
}

TEST(RseLcmTest, ConfiguredTciTpModeIsExposed)
{
    cci::cci_get_global_broker(cci::cci_originator("rse_lcm_test"))
        .set_preset_cci_value("rse_lcm_tci.tp_mode", cci::cci_value(0x111155AAu));

    rse_lcm dut("rse_lcm_tci");

    EXPECT_EQ(read32(dut, TP_MODE), 0x111155AAu);
}

TEST(RseLcmTest, SecureProvisioningMagicSelfCompletes)
{
    rse_lcm dut("rse_lcm_sp");

    write32(dut, SP_ENABLE, 0x5ec10e1eu);
    EXPECT_EQ(read32(dut, SP_ENABLE), 0xffffffffu);
}

TEST(RseLcmTest, LifecycleIdentityRegistersIgnoreWrites)
{
    rse_lcm dut("rse_lcm_ro");

    write32(dut, LCS_VALUE, 0xcccc3c3cu);
    write32(dut, TP_MODE, 0x000033ccu);
    write32(dut, OTP_SIZE, 0x00000020u);

    EXPECT_EQ(read32(dut, LCS_VALUE), 0xeeeea5a5u);
    EXPECT_EQ(read32(dut, TP_MODE), 0x2222aa55u);
    EXPECT_EQ(read32(dut, OTP_SIZE), 0x00010000u);
}

TEST(RseLcmTest, OtpWindowIsWritable)
{
    rse_lcm dut("rse_lcm_otp");

    write32(dut, OTP_WINDOW, 0x01234567u);
    EXPECT_EQ(read32(dut, OTP_WINDOW), 0x01234567u);
}

TEST(RseLcmTest, OtpImageParameterLoadsOtpWindow)
{
    const std::string otp_path = ::testing::TempDir() + "/rse_lcm_otp_image.bin";
    const std::vector<uint8_t> otp = {
        0x67, 0x45, 0x23, 0x01,
        0xef, 0xcd, 0xab, 0x89,
    };

    {
        std::ofstream otp_file(otp_path, std::ios::binary);
        ASSERT_TRUE(otp_file);
        otp_file.write(reinterpret_cast<const char*>(otp.data()), otp.size());
        ASSERT_TRUE(otp_file);
    }

    cci::cci_get_global_broker(cci::cci_originator("rse_lcm_test"))
        .set_preset_cci_value("rse_lcm_otp_image.otp_image",
                              cci::cci_value(otp_path));

    rse_lcm dut("rse_lcm_otp_image");

    EXPECT_EQ(read32(dut, OTP_WINDOW), 0x01234567u);
    EXPECT_EQ(read32(dut, OTP_WINDOW + sizeof(uint32_t)), 0x89abcdefu);
}

TEST(RseLcmTest, OtpWritebackUpdatesImageFile)
{
    const std::string otp_path = ::testing::TempDir() + "/rse_lcm_otp_writeback.bin";
    const std::vector<uint8_t> otp(8, 0);

    {
        std::ofstream otp_file(otp_path, std::ios::binary);
        ASSERT_TRUE(otp_file);
        otp_file.write(reinterpret_cast<const char*>(otp.data()), otp.size());
        ASSERT_TRUE(otp_file);
    }

    cci::cci_get_global_broker(cci::cci_originator("rse_lcm_test"))
        .set_preset_cci_value("rse_lcm_otp_writeback.otp_image",
                              cci::cci_value(otp_path));
    cci::cci_get_global_broker(cci::cci_originator("rse_lcm_test"))
        .set_preset_cci_value("rse_lcm_otp_writeback.otp_writeback",
                              cci::cci_value(true));

    rse_lcm dut("rse_lcm_otp_writeback");

    write32(dut, OTP_WINDOW, 0xa5a55a5au);

    EXPECT_EQ(read32(dut, OTP_WINDOW), 0xa5a55a5au);
    EXPECT_EQ(read_file32(otp_path), 0xa5a55a5au);
}

TEST(RseLcmTest, OtpLocksAfterProvisioningCompletes)
{
    rse_lcm dut("rse_lcm_otp_lock");

    write32(dut, OTP_WINDOW, 0x11112222u);
    write32(dut, SP_ENABLE, 0x5ec10e1eu);
    write32(dut, OTP_WINDOW, 0x33334444u);

    EXPECT_EQ(read32(dut, SP_ENABLE), 0xffffffffu);
    EXPECT_EQ(read32(dut, OTP_WINDOW), 0x11112222u);
}

TEST(RseLcmTest, RejectsOutOfRangeTransactions)
{
    rse_lcm dut("rse_lcm_bounds");
    tlm::tlm_generic_payload trans;
    uint32_t data = 0;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(0x11000 - 1);
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
