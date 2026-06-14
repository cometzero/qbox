/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <rse_protection_ctrl.h>
#include <systemc>
#include <tlm>

namespace {

constexpr uint64_t STATUS = 0x000;
constexpr uint64_t CLEAR = 0x004;
constexpr uint64_t LOCK = 0x010;
constexpr uint64_t BLK_MAX = 0x014;
constexpr uint64_t BLK_CFG = 0x018;
constexpr uint64_t TEST_REG = 0x020;
constexpr uint64_t PIDR4 = 0xfd0;
constexpr uint64_t PIDR0 = 0xfe0;
constexpr uint64_t PIDR1 = 0xfe4;
constexpr uint64_t PIDR2 = 0xfe8;
constexpr uint64_t PIDR3 = 0xfec;
constexpr uint64_t CIDR0 = 0xff0;
constexpr uint64_t CIDR1 = 0xff4;
constexpr uint64_t CIDR2 = 0xff8;
constexpr uint64_t CIDR3 = 0xffc;

uint32_t access32(rse_protection_ctrl& dut, uint64_t offset,
                  tlm::tlm_command command, uint32_t value = 0,
                  bool non_secure = false,
                  tlm::tlm_response_status expected = tlm::TLM_OK_RESPONSE)
{
    tlm::tlm_generic_payload trans;
    auto data = value;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    if (non_secure) {
        dut.non_secure_b_transport(trans, delay);
    } else {
        dut.b_transport(trans, delay);
    }

    EXPECT_EQ(trans.get_response_status(), expected);
    return data;
}

uint32_t read32(rse_protection_ctrl& dut, uint64_t offset, bool non_secure = false)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND, 0, non_secure);
}

void write32(rse_protection_ctrl& dut, uint64_t offset, uint32_t value,
             bool non_secure = false,
             tlm::tlm_response_status expected = tlm::TLM_OK_RESPONSE)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value, non_secure,
                   expected);
}

}

TEST(RseProtectionCtrlTest, MpcProfilePreservesSeededResetValues)
{
    auto broker =
        cci::cci_get_global_broker(cci::cci_originator("rse_protection_test"));
    broker.set_preset_cci_value("mpc.profile", cci::cci_value(1u));
    broker.set_preset_cci_value("mpc.blk_max", cci::cci_value(127u));

    rse_protection_ctrl dut("mpc");
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, BLK_MAX), 127u);
    EXPECT_EQ(read32(dut, BLK_CFG), 0x7u);
    EXPECT_EQ(read32(dut, PIDR4), 0x04u);
    EXPECT_EQ(read32(dut, PIDR0), 0x65u);
    EXPECT_EQ(read32(dut, PIDR1), 0xb8u);
    EXPECT_EQ(read32(dut, PIDR2), 0x0bu);
    EXPECT_EQ(read32(dut, PIDR3), 0x00u);
    EXPECT_EQ(read32(dut, CIDR0), 0x0du);
    EXPECT_EQ(read32(dut, CIDR1), 0xf0u);
    EXPECT_EQ(read32(dut, CIDR2), 0x05u);
    EXPECT_EQ(read32(dut, CIDR3), 0xb1u);
}

TEST(RseProtectionCtrlTest, LockPreventsFurtherWrites)
{
    auto broker =
        cci::cci_get_global_broker(cci::cci_originator("rse_protection_test"));
    broker.set_preset_cci_value("lock_test.lock_mask", cci::cci_value(1u));

    rse_protection_ctrl dut("lock_test");
    dut.before_end_of_elaboration();

    write32(dut, TEST_REG, 0x11223344u);
    EXPECT_EQ(read32(dut, TEST_REG), 0x11223344u);

    write32(dut, LOCK, 0x1u);
    write32(dut, TEST_REG, 0x55667788u);
    EXPECT_EQ(read32(dut, TEST_REG), 0x11223344u);
}

TEST(RseProtectionCtrlTest, NonSecureWriteCanBeDeniedAndLatched)
{
    auto broker =
        cci::cci_get_global_broker(cci::cci_originator("rse_protection_test"));
    broker.set_preset_cci_value("ns_deny.error_latch", cci::cci_value(true));

    rse_protection_ctrl dut("ns_deny");
    dut.before_end_of_elaboration();

    write32(dut, TEST_REG, 0x55u, true, tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(read32(dut, TEST_REG), 0u);
    EXPECT_EQ(read32(dut, STATUS) & 0x1u, 0x1u);

    write32(dut, CLEAR, 0x1u);
    EXPECT_EQ(read32(dut, STATUS), 0u);
}

TEST(RseProtectionCtrlTest, NonSecureWriteCanBeEnabledForCompatibility)
{
    auto broker =
        cci::cci_get_global_broker(cci::cci_originator("rse_protection_test"));
    broker.set_preset_cci_value("ns_allow.non_secure_writes",
                                cci::cci_value(true));

    rse_protection_ctrl dut("ns_allow");
    dut.before_end_of_elaboration();

    write32(dut, TEST_REG, 0xa5a50011u, true);
    EXPECT_EQ(read32(dut, TEST_REG), 0xa5a50011u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
