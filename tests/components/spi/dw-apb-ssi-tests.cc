/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <dw-apb-ssi.h>
#include <cci/utils/broker.h>
#include <tests/initiator-tester.h>
#include <tests/test-bench.h>

class DwApbSsiTestBench : public TestBench
{
protected:
    dw_apb_ssi dut;
    InitiatorTester initiator;
    sc_core::sc_signal<bool> irq_signal;

    void write32(uint32_t offset, uint32_t value)
    {
        ASSERT_EQ(initiator.do_write(offset, value), tlm::TLM_OK_RESPONSE);
    }

    uint32_t read32(uint32_t offset)
    {
        uint32_t value = 0;
        EXPECT_EQ(initiator.do_read(offset, value), tlm::TLM_OK_RESPONSE);
        return value;
    }

public:
    explicit DwApbSsiTestBench(sc_core::sc_module_name name)
        : TestBench(name), dut("dut"), initiator("initiator"), irq_signal("irq")
    {
        initiator.socket.bind(dut.target_socket);
        dut.irq.bind(irq_signal);
    }
};

TEST_BENCH(DwApbSsiTestBench, ProbeAndLoopback)
{
    EXPECT_EQ(read32(dw_apb_ssi::VERSION), dw_apb_ssi::COMPONENT_VERSION);

    unsigned int fifo_depth = 0;
    for (unsigned int fifo = 1; fifo < 256; ++fifo) {
        write32(dw_apb_ssi::TXFTLR, fifo);
        if (read32(dw_apb_ssi::TXFTLR) != fifo) {
            fifo_depth = fifo;
            break;
        }
    }
    EXPECT_EQ(fifo_depth, dw_apb_ssi::DEFAULT_FIFO_DEPTH);

    write32(dw_apb_ssi::SSIENR, 0);
    write32(dw_apb_ssi::CTRLR0, 7 | (1u << 11));
    write32(dw_apb_ssi::BAUDR, 2);
    write32(dw_apb_ssi::RXFTLR, 0);
    write32(dw_apb_ssi::IMR, dw_apb_ssi::INT_RXFI);
    write32(dw_apb_ssi::SER, 1);
    write32(dw_apb_ssi::SSIENR, 1);
    write32(dw_apb_ssi::DR, 0xa5);

    wait(sc_core::sc_time(1, sc_core::SC_US));
    EXPECT_EQ(read32(dw_apb_ssi::RXFLR), 1u);
    EXPECT_TRUE(irq_signal.read());
    EXPECT_NE(read32(dw_apb_ssi::ISR) & dw_apb_ssi::INT_RXFI, 0u);
    EXPECT_EQ(read32(dw_apb_ssi::DR), 0xa5u);
    wait(sc_core::sc_time(1, sc_core::SC_NS));
    EXPECT_FALSE(irq_signal.read());
}

TEST_BENCH(DwApbSsiTestBench, InterruptClearAndReset)
{
    write32(dw_apb_ssi::IMR, dw_apb_ssi::INT_RXUI);
    EXPECT_EQ(read32(dw_apb_ssi::DR), 0u);
    wait(sc_core::sc_time(1, sc_core::SC_NS));
    EXPECT_TRUE(irq_signal.read());
    EXPECT_EQ(read32(dw_apb_ssi::RXUICR), dw_apb_ssi::INT_RXUI);
    wait(sc_core::sc_time(1, sc_core::SC_NS));
    EXPECT_FALSE(irq_signal.read());

    write32(dw_apb_ssi::SSIENR, 1);
    dut.reset->write(true);
    wait(sc_core::sc_time(1, sc_core::SC_NS));
    EXPECT_EQ(read32(dw_apb_ssi::SSIENR), 0u);
    EXPECT_EQ(read32(dw_apb_ssi::SR), dw_apb_ssi::SR_TF_NOT_FULL | dw_apb_ssi::SR_TF_EMPTY);
}

TEST_BENCH(DwApbSsiTestBench, LinuxStyleLongLoopback)
{
    constexpr uint32_t transfer_size = 4096;
    constexpr uint32_t fifo_depth = dw_apb_ssi::DEFAULT_FIFO_DEPTH;
    uint32_t sent = 0;
    uint32_t received = 0;
    uint32_t interrupts = 0;
    const sc_core::sc_time start = sc_core::sc_time_stamp();

    write32(dw_apb_ssi::SSIENR, 0);
    write32(dw_apb_ssi::CTRLR0, 7 | (1u << 11));
    write32(dw_apb_ssi::BAUDR, 100);
    write32(dw_apb_ssi::TXFTLR, fifo_depth / 2);
    write32(dw_apb_ssi::RXFTLR, fifo_depth / 2 - 1);
    write32(dw_apb_ssi::SER, 1);
    write32(dw_apb_ssi::SSIENR, 1);
    write32(dw_apb_ssi::IMR, dw_apb_ssi::INT_TXEI | dw_apb_ssi::INT_TXOI | dw_apb_ssi::INT_RXUI | dw_apb_ssi::INT_RXOI |
                                 dw_apb_ssi::INT_RXFI);

    while (received < transfer_size) {
        if (!irq_signal.read()) {
            wait(sc_core::sc_time(1, sc_core::SC_MS), irq_signal.posedge_event());
            ASSERT_TRUE(irq_signal.read())
                << "sent=" << sent << " received=" << received << " txflr=" << read32(dw_apb_ssi::TXFLR)
                << " rxflr=" << read32(dw_apb_ssi::RXFLR) << " risr=" << read32(dw_apb_ssi::RISR);
        }

        const uint32_t isr = read32(dw_apb_ssi::ISR);
        ASSERT_EQ(isr & (dw_apb_ssi::INT_TXOI | dw_apb_ssi::INT_RXUI | dw_apb_ssi::INT_RXOI), 0u);
        ++interrupts;

        while (read32(dw_apb_ssi::RXFLR)) {
            EXPECT_EQ(read32(dw_apb_ssi::DR), received & 0xff);
            ++received;
        }
        const uint32_t remaining = transfer_size - received;
        if (remaining && remaining <= read32(dw_apb_ssi::RXFTLR)) {
            write32(dw_apb_ssi::RXFTLR, remaining - 1);
        }

        if (isr & dw_apb_ssi::INT_TXEI) {
            while (sent < transfer_size && read32(dw_apb_ssi::TXFLR) < fifo_depth && sent - received < fifo_depth) {
                write32(dw_apb_ssi::DR, sent & 0xff);
                ++sent;
            }
            if (sent == transfer_size) {
                write32(dw_apb_ssi::IMR, read32(dw_apb_ssi::IMR) & ~dw_apb_ssi::INT_TXEI);
            }
        }
        wait(sc_core::SC_ZERO_TIME);
        wait(sc_core::SC_ZERO_TIME);
    }

    write32(dw_apb_ssi::IMR, 0);
    wait(sc_core::sc_time(1, sc_core::SC_NS));
    EXPECT_EQ(sent, transfer_size);
    EXPECT_EQ(received, transfer_size);
    EXPECT_EQ(interrupts, 513u);
    EXPECT_EQ(sc_core::sc_time_stamp() - start, sc_core::sc_time(32768001, sc_core::SC_NS));
    EXPECT_FALSE(irq_signal.read());
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
