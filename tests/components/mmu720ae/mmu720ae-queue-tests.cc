/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

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

uint32_t read32(mmu720ae& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(mmu720ae& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(Mmu720AeQueueTest, CmdqProducerAdvancesConsumerForSyncCompletion)
{
    mmu720ae dut("mmu720ae_cmdq");
    const uint64_t generation = dut.core_for_test().dmi_generation();

    write32(dut, qbox::mmu720ae::ARM_SMMU_CMDQ_CONS, 0);
    write32(dut, qbox::mmu720ae::ARM_SMMU_CMDQ_PROD, 7);

    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_CMDQ_PROD), 7u);
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_CMDQ_CONS), 7u);
    EXPECT_EQ(dut.core_for_test().cmdq_sync_count(), 1u);
    EXPECT_GT(dut.core_for_test().dmi_generation(), generation);
}

TEST(Mmu720AeQueueTest, IrqCtrlAckMirrorsImplementedBits)
{
    mmu720ae dut("mmu720ae_irq");

    write32(dut, qbox::mmu720ae::ARM_SMMU_IRQ_CTRL,
            qbox::mmu720ae::IRQ_CTRL_EVTQ_IRQEN |
                qbox::mmu720ae::IRQ_CTRL_PRIQ_IRQEN |
                qbox::mmu720ae::IRQ_CTRL_GERROR_IRQEN);

    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_IRQ_CTRL),
              qbox::mmu720ae::IRQ_CTRL_EVTQ_IRQEN |
                  qbox::mmu720ae::IRQ_CTRL_GERROR_IRQEN);
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_IRQ_CTRLACK),
              read32(dut, qbox::mmu720ae::ARM_SMMU_IRQ_CTRL));
}

TEST(Mmu720AeQueueTest, GerrornWriteClearsGlobalErrorBits)
{
    mmu720ae dut("mmu720ae_gerror");

    write32(dut, qbox::mmu720ae::ARM_SMMU_GERROR,
            qbox::mmu720ae::GERROR_CMDQ_ERR);
    EXPECT_NE(read32(dut, qbox::mmu720ae::ARM_SMMU_GERROR), 0u);

    write32(dut, qbox::mmu720ae::ARM_SMMU_GERRORN,
            qbox::mmu720ae::GERROR_CMDQ_ERR);
    EXPECT_EQ(read32(dut, qbox::mmu720ae::ARM_SMMU_GERROR) &
                  qbox::mmu720ae::GERROR_CMDQ_ERR,
              0u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
