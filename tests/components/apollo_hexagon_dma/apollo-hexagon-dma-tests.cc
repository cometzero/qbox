/*
 * Apollo Hexagon DMA component tests.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>

#include <libgsutils.h>

#include <apollo_hexagon_dma.h>

#include <ports/target-signal-socket.h>
#include <tests/initiator-tester.h>
#include <tests/test-bench.h>

class ApolloHexagonDmaTestBench : public TestBench
{
protected:
    enum : uint64_t {
        REG_JOB_STATUS = 0x54,
        REG_JOB_QUEUE = 0x5c,
        REG_JOB_FENCE = 0x60,
        REG_IRQ_STATUS = 0x64,
        REG_IRQ_ACK = 0x68,
    };

    enum : uint32_t {
        JOB_STATUS_DONE = 1,
        QUEUE_DMA = 0,
        QUEUE_CNN = 1,
    };

    apollo_hexagon_dma m_dma;
    InitiatorTester m_regs;
    TargetSignalSocket<bool> m_irq_line;

    void write32(uint64_t addr, uint32_t value)
    {
        const auto status = m_regs.do_write_with_ptr(
            addr, reinterpret_cast<const uint8_t*>(&value), sizeof(value));

        ASSERT_EQ(tlm::TLM_OK_RESPONSE, status);
    }

    uint32_t read32(uint64_t addr)
    {
        uint32_t value = 0;
        const auto status = m_regs.do_read_with_ptr(
            addr, reinterpret_cast<uint8_t*>(&value), sizeof(value));

        EXPECT_EQ(tlm::TLM_OK_RESPONSE, status);
        return value;
    }

public:
    explicit ApolloHexagonDmaTestBench(const sc_core::sc_module_name& n)
        : TestBench(n)
        , m_dma("dma")
        , m_regs("regs")
        , m_irq_line("irq_line")
    {
        m_regs.socket.bind(m_dma.regs);
        m_dma.irq_out.bind(m_irq_line);
    }
};

TEST_BENCH(ApolloHexagonDmaTestBench, AsyncFenceDrivesIrqSignalUntilAck)
{
    EXPECT_FALSE(m_irq_line.read());
    EXPECT_EQ(0u, read32(REG_IRQ_STATUS));

    write32(REG_JOB_QUEUE, QUEUE_DMA);
    write32(REG_JOB_STATUS, JOB_STATUS_DONE);

    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_DMA, read32(REG_IRQ_STATUS));
    EXPECT_EQ(1u, read32(REG_JOB_FENCE));

    write32(REG_JOB_QUEUE, QUEUE_CNN);
    write32(REG_JOB_STATUS, JOB_STATUS_DONE);

    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ((1u << QUEUE_DMA) | (1u << QUEUE_CNN), read32(REG_IRQ_STATUS));
    EXPECT_EQ(2u, read32(REG_JOB_FENCE));

    write32(REG_IRQ_ACK, 1u << QUEUE_DMA);
    EXPECT_TRUE(m_irq_line.read());
    EXPECT_EQ(1u << QUEUE_CNN, read32(REG_IRQ_STATUS));

    write32(REG_IRQ_ACK, 1u << QUEUE_CNN);
    EXPECT_FALSE(m_irq_line.read());
    EXPECT_EQ(0u, read32(REG_IRQ_STATUS));
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker {};

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
