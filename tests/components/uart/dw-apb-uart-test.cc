/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <systemc>

#include <dw-apb-uart.h>
#include <ports/initiator-signal-socket.h>
#include <tests/initiator-tester.h>
#include <tests/test-bench.h>

class DwApbUartTest : public TestBench
{
protected:
    dw_apb_uart uart0;
    dw_apb_uart uart1;
    InitiatorTester bus0;
    InitiatorTester bus1;
    InitiatorSignalSocket<bool> reset0;
    InitiatorSignalSocket<bool> reset1;
    sc_core::sc_signal<bool> irq0;
    sc_core::sc_signal<bool> irq1;

    static constexpr uint32_t RBR_THR_DLL = 0x00;
    static constexpr uint32_t IER_DLH = 0x04;
    static constexpr uint32_t IIR_FCR = 0x08;
    static constexpr uint32_t LCR = 0x0c;
    static constexpr uint32_t MCR = 0x10;
    static constexpr uint32_t LSR = 0x14;
    static constexpr uint32_t USR = 0x7c;
    static constexpr uint32_t RFL = 0x84;
    static constexpr uint32_t CPR = 0xf4;
    static constexpr uint32_t UCV = 0xf8;
    static constexpr uint32_t CTR = 0xfc;

    static constexpr uint32_t IER_RDI = 0x01;
    static constexpr uint32_t IER_THRI = 0x02;
    static constexpr uint32_t FCR_ENABLE_CLEAR = 0x07;
    static constexpr uint32_t FCR_TRIGGER_4 = 0x47;
    static constexpr uint32_t LCR_DLAB = 0x80;
    static constexpr uint32_t LCR_8N1 = 0x03;
    static constexpr uint32_t MCR_LOOP = 0x10;

    void write(InitiatorTester& bus, uint32_t address, uint32_t value)
    {
        bus.set_next_txn_delay(sc_core::SC_ZERO_TIME);
        ASSERT_EQ(bus.do_write(address, value), tlm::TLM_OK_RESPONSE);
        EXPECT_EQ(bus.get_last_txn_delay(), sc_core::sc_time(10, sc_core::SC_NS));
    }

    uint32_t read(InitiatorTester& bus, uint32_t address)
    {
        uint32_t value = 0;
        bus.set_next_txn_delay(sc_core::SC_ZERO_TIME);
        EXPECT_EQ(bus.do_read(address, value), tlm::TLM_OK_RESPONSE);
        EXPECT_EQ(bus.get_last_txn_delay(), sc_core::sc_time(10, sc_core::SC_NS));
        return value;
    }

    void linux_init(InitiatorTester& bus, uint32_t ier)
    {
        write(bus, IER_DLH, 0);
        write(bus, LCR, LCR_DLAB);
        write(bus, RBR_THR_DLL, 1);
        write(bus, IER_DLH, 0);
        write(bus, LCR, LCR_8N1);
        write(bus, IIR_FCR, FCR_ENABLE_CLEAR);
        write(bus, IER_DLH, ier);
    }

    void settle_irq()
    {
        sc_core::wait(sc_core::SC_ZERO_TIME);
        sc_core::wait(sc_core::SC_ZERO_TIME);
    }

public:
    explicit DwApbUartTest(sc_core::sc_module_name name)
        : TestBench(name)
        , uart0("uart0")
        , uart1("uart1")
        , bus0("bus0")
        , bus1("bus1")
        , reset0("reset0")
        , reset1("reset1")
        , irq0("irq0")
        , irq1("irq1")
    {
        bus0.socket.bind(uart0.target_socket);
        bus1.socket.bind(uart1.target_socket);
        uart0.backend_socket.bind(uart1.backend_socket);
        uart0.irq.bind(irq0);
        uart1.irq.bind(irq1);
        reset0.bind(uart0.reset);
        reset1.bind(uart1.reset);
    }
};

TEST_BENCH(DwApbUartTest, LinuxDriverAndPairedTransfer)
{
    uint32_t invalid_word = 0;
    uint16_t invalid_halfword = 0;
    EXPECT_EQ(bus0.do_read(2, invalid_word), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(bus0.do_read(RBR_THR_DLL, invalid_halfword), tlm::TLM_BURST_ERROR_RESPONSE);

    EXPECT_EQ(read(bus0, CPR), 0x00010002u);
    EXPECT_FALSE(bus0.get_last_dmi_hint());
    EXPECT_EQ(read(bus0, UCV), 0x3430352au);
    EXPECT_EQ(read(bus0, CTR), 0x44570110u);
    EXPECT_EQ(read(bus0, USR) & 0x06, 0x06u);

    linux_init(bus0, IER_THRI);
    linux_init(bus1, IER_RDI);
    settle_irq();
    EXPECT_TRUE(irq0.read());
    EXPECT_EQ(read(bus0, IIR_FCR), 0xc2u);
    settle_irq();
    EXPECT_FALSE(irq0.read());

    uart0.pinmux_enable->write(false);
    write(bus0, RBR_THR_DLL, uint32_t('D'));
    sc_core::wait(3, sc_core::SC_US);
    EXPECT_EQ(read(bus1, RFL), 0u);

    write(bus0, MCR, MCR_LOOP);
    write(bus0, RBR_THR_DLL, uint32_t('L'));
    sc_core::wait(3, sc_core::SC_US);
    EXPECT_EQ(read(bus0, RBR_THR_DLL), uint32_t('L'));
    write(bus0, MCR, 0);
    uart0.pinmux_enable->write(true);

    write(bus0, RBR_THR_DLL, uint32_t('A'));
    sc_core::wait(3, sc_core::SC_US);
    EXPECT_EQ(read(bus1, LSR) & 0x01, 0x01u);
    EXPECT_EQ(read(bus1, RBR_THR_DLL), uint32_t('A'));

    uart1.pinmux_enable->write(false);
    for (unsigned int i = 0; i < dw_apb_uart::FIFO_DEPTH + 2; ++i) {
        write(bus0, RBR_THR_DLL, uint32_t('I'));
        sc_core::wait(3, sc_core::SC_US);
    }
    EXPECT_EQ(read(bus1, RFL), 0u);
    uart1.pinmux_enable->write(true);
    settle_irq();
    write(bus0, RBR_THR_DLL, uint32_t('N'));
    sc_core::wait(3, sc_core::SC_US);
    EXPECT_EQ(read(bus1, RBR_THR_DLL), uint32_t('N'));

    write(bus1, RBR_THR_DLL, uint32_t('B'));
    sc_core::wait(3, sc_core::SC_US);
    EXPECT_EQ(read(bus0, RBR_THR_DLL), uint32_t('B'));

    write(bus0, IER_DLH, 0);
    write(bus1, IIR_FCR, FCR_TRIGGER_4);
    for (uint32_t value = 0; value < 4; ++value) {
        write(bus0, RBR_THR_DLL, 0x30 + value);
    }
    sc_core::wait(8, sc_core::SC_US);
    EXPECT_EQ(read(bus1, RFL), 4u);
    EXPECT_TRUE(irq1.read());
    EXPECT_EQ(read(bus1, IIR_FCR), 0xc4u);
    for (uint32_t value = 0; value < 4; ++value) {
        EXPECT_EQ(read(bus1, RBR_THR_DLL), 0x30 + value);
    }
    settle_irq();
    EXPECT_FALSE(irq1.read());

    write(bus0, RBR_THR_DLL, uint32_t('T'));
    sc_core::wait(3, sc_core::SC_US);
    EXPECT_FALSE(irq1.read());
    sc_core::wait(7, sc_core::SC_US);
    EXPECT_TRUE(irq1.read());
    EXPECT_EQ(read(bus1, IIR_FCR), 0xccu);
    EXPECT_EQ(read(bus1, RBR_THR_DLL), uint32_t('T'));
    settle_irq();
    EXPECT_FALSE(irq1.read());

    write(bus0, RBR_THR_DLL, uint32_t('X'));
    write(bus0, LCR, 0x1b);
    EXPECT_EQ(read(bus0, LCR), LCR_8N1);
    EXPECT_EQ(read(bus0, IIR_FCR), 0xc7u);
    EXPECT_NE(read(bus0, USR) & 0x01, 0u);
    write(bus0, IIR_FCR, FCR_ENABLE_CLEAR);
    write(bus0, LCR, 0x1b);
    EXPECT_EQ(read(bus0, LCR), 0x1bu);

    reset1->write(true);
    settle_irq();
    EXPECT_EQ(read(bus1, RFL), 0u);
    EXPECT_EQ(read(bus1, LSR), 0x60u);
    EXPECT_FALSE(irq1.read());
    reset1->write(false);

    sc_core::sc_stop();
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker{};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
