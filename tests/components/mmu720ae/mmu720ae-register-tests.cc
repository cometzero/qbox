/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <systemc>
#include <tlm>

#include <mmu720ae.h>
#include <mmu720ae_regs.h>

namespace {

uint32_t access32(mmu720ae& dut, uint64_t offset, tlm::tlm_command command,
                  uint32_t value = 0)
{
    tlm::tlm_generic_payload trans;
    uint32_t data = value;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint64_t access64(mmu720ae& dut, uint64_t offset, tlm::tlm_command command,
                  uint64_t value = 0)
{
    tlm::tlm_generic_payload trans;
    uint64_t data = value;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    dut.b_transport(trans, delay);
    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint32_t read32(mmu720ae& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

uint64_t read64(mmu720ae& dut, uint64_t offset)
{
    return access64(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(mmu720ae& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

void write64(mmu720ae& dut, uint64_t offset, uint64_t value)
{
    (void)access64(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(Mmu720AeRegisterTest, ResetProfileExposesImplementedProbeFeatures)
{
    mmu720ae dut("mmu720ae_reset");

    const uint32_t idr0 = read32(dut, qbox::mmu720ae::ARM_SMMU_IDR0);
    EXPECT_NE(idr0 & qbox::mmu720ae::IDR0_S1P, 0u);
    EXPECT_NE(idr0 & qbox::mmu720ae::IDR0_COHACC, 0u);
    EXPECT_EQ(idr0 & qbox::mmu720ae::IDR0_ATS, 0u);
    EXPECT_EQ(idr0 & qbox::mmu720ae::IDR0_PRI, 0u);
    EXPECT_EQ(idr0 & qbox::mmu720ae::IDR0_MSI, 0u);

    const uint32_t idr1 = read32(dut, qbox::mmu720ae::ARM_SMMU_IDR1);
    EXPECT_EQ(idr1 & 0x3fu, 8u);
    EXPECT_EQ((idr1 >> qbox::mmu720ae::IDR1_SSIDSIZE_SHIFT) & 0x1fu, 0u);
    EXPECT_GE((idr1 >> qbox::mmu720ae::IDR1_CMDQS_SHIFT) & 0x1fu, 7u);
    EXPECT_EQ((idr1 >> qbox::mmu720ae::IDR1_PRIQS_SHIFT) & 0x1fu, 0u);

    const uint32_t idr5 = read32(dut, qbox::mmu720ae::ARM_SMMU_IDR5);
    EXPECT_NE(idr5 & qbox::mmu720ae::IDR5_GRAN4K, 0u);
    EXPECT_EQ(idr5 & 0x7u, qbox::mmu720ae::IDR5_OAS_48_BIT);
}

TEST(Mmu720AeRegisterTest, Cr0WriteUpdatesCr0AckAndInvalidatesGeneration)
{
    mmu720ae dut("mmu720ae_cr0");
    const uint64_t generation = dut.core_for_test().dmi_generation();

    write32(dut, qbox::mmu720ae::ARM_SMMU_CR0,
            qbox::mmu720ae::CR0_CMDQEN | qbox::mmu720ae::CR0_EVTQEN |
                qbox::mmu720ae::CR0_SMMUEN | qbox::mmu720ae::CR0_PRIQEN |
                qbox::mmu720ae::CR0_ATSCHK);

    const uint32_t expected = qbox::mmu720ae::CR0_CMDQEN |
                              qbox::mmu720ae::CR0_EVTQEN |
                              qbox::mmu720ae::CR0_SMMUEN;
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_CR0), expected);
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_CR0ACK), expected);

    write32(dut, qbox::mmu720ae::ARM_SMMU_CR0, 0);
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_CR0ACK), 0u);
    EXPECT_GT(dut.core_for_test().dmi_generation(), generation);
}

TEST(Mmu720AeRegisterTest, Page1AliasAccessesQueueRegisters)
{
    mmu720ae dut("mmu720ae_page1");

    write32(dut,
            qbox::mmu720ae::ARM_SMMU_PAGE1_OFFSET +
                qbox::mmu720ae::ARM_SMMU_EVTQ_CONS,
            0x4);
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_EVTQ_CONS), 0x4u);
}

TEST(Mmu720AeRegisterTest, QueueAndMsiConfigAccept64BitWrites)
{
    mmu720ae dut("mmu720ae_64");

    write64(dut, qbox::mmu720ae::ARM_SMMU_STRTAB_BASE, 0x400000008000ull);
    write64(dut, qbox::mmu720ae::ARM_SMMU_CMDQ_BASE, 0x400000009000ull);
    write64(dut, qbox::mmu720ae::ARM_SMMU_EVTQ_IRQ_CFG0, 0xfee00000ull);

    EXPECT_EQ(read64(dut, qbox::mmu720ae::ARM_SMMU_STRTAB_BASE),
              0x400000008000ull);
    EXPECT_EQ(read64(dut, qbox::mmu720ae::ARM_SMMU_CMDQ_BASE),
              0x400000009000ull);
    EXPECT_EQ(read64(dut, qbox::mmu720ae::ARM_SMMU_EVTQ_IRQ_CFG0),
              0xfee00000ull);
}

TEST(Mmu720AeRegisterTest, UnsupportedAccessReturnsAddressError)
{
    mmu720ae dut("mmu720ae_bad_access");
    tlm::tlm_generic_payload trans;
    uint32_t data = 0;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

    trans.set_address(0x20000);
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

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
