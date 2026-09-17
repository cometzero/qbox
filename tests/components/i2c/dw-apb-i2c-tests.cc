/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <cci/utils/broker.h>
#include <tests/test-bench.h>

#include "dw-apb-i2c.h"
#include "i2c-bus.h"

// Records the controller's wire ordering independently of EEPROM parser state.
class I2cEventTarget : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<I2cEventTarget> socket;
    std::vector<std::string> events;
    explicit I2cEventTarget(sc_core::sc_module_name name) : sc_module(name), socket("socket")
    {
        socket.register_b_transport(this, &I2cEventTarget::transport);
    }
    void transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        auto* ext = trans.get_extension<dw_i2c_extension>();
        using event = dw_i2c_extension::event;
        const auto phase = ext ? ext->phase : event::data;
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        if (phase == event::discover || phase == event::address) {
            if (trans.get_address() != 0x54) trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            else if (phase == event::address) events.push_back("address");
            return;
        }
        switch (phase) {
        case event::data:
            if (trans.is_read()) *trans.get_data_ptr() = 0x42;
            events.push_back(trans.is_read() ? "read" : "write");
            break;
        case event::read_ack: events.push_back(ext->ack ? "ack" : "nack"); break;
        case event::start: events.push_back("start"); break;
        case event::restart: events.push_back("restart"); break;
        case event::stop: events.push_back("stop"); break;
        case event::cancel: events.push_back("cancel"); break;
        default: break;
        }
    }
};

class DwApbI2cBench : public TestBench
{
protected:
    tlm_utils::simple_initiator_socket<DwApbI2cBench> cpu_socket;
    dw_apb_i2c controller;
    i2c_bus bus;
    dw_i2c_eeprom eeprom;
    dw_i2c_eeprom eeprom1;
    dw_i2c_eeprom eeprom2;
    I2cEventTarget observer;
    sc_core::sc_signal<bool> irq;
    sc_core::sc_signal<uint32_t> dma_tx_req;
    sc_core::sc_signal<uint32_t> dma_rx_req;

    static void set_eeprom_presets(const std::string& bench)
    {
        auto broker = cci::cci_get_broker();
        broker.set_preset_cci_value(bench + ".eeprom2.address", cci::cci_value(0x52));
        broker.set_preset_cci_value(bench + ".eeprom2.size", cci::cci_value(512));
        broker.set_preset_cci_value(bench + ".eeprom2.address_width", cci::cci_value(16));
        broker.set_preset_cci_value(bench + ".eeprom2.page_size", cci::cci_value(64));
    }

    explicit DwApbI2cBench(const sc_core::sc_module_name& name)
        : TestBench(name)
        , cpu_socket("cpu_socket")
        , controller("controller")
        , bus("bus")
        , eeprom("eeprom")
        , eeprom1("eeprom1")
        , eeprom2((set_eeprom_presets(this->name()), "eeprom2"))
        , observer("observer")
        , irq("irq")
        , dma_tx_req("dma_tx_req")
        , dma_rx_req("dma_rx_req")
    {
        cpu_socket.bind(controller.target_socket);
        controller.i2c_socket.bind(bus.target_socket);
        bus.initiator_socket.bind(eeprom.i2c_socket);
        bus.initiator_socket.bind(eeprom1.i2c_socket);
        bus.initiator_socket.bind(eeprom2.i2c_socket);
        bus.initiator_socket.bind(observer.socket);
        eeprom1.p_address.set_cci_value(cci::cci_value(0x51));

        controller.irq.bind(irq);
        controller.dma_tx_req.bind(dma_tx_req);
        controller.dma_rx_req.bind(dma_rx_req);
    }

    tlm::tlm_response_status access(tlm::tlm_command command, uint64_t address, uint32_t& value,
                                    unsigned length = sizeof(value), unsigned streaming_width = sizeof(value),
                                    unsigned char* byte_enable = nullptr)
    {
        tlm::tlm_generic_payload trans;
        trans.set_command(command);
        trans.set_address(address);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        trans.set_data_length(length);
        trans.set_streaming_width(streaming_width);
        trans.set_byte_enable_ptr(byte_enable);
        trans.set_byte_enable_length(byte_enable ? 1 : 0);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        cpu_socket->b_transport(trans, delay);
        sc_core::wait(delay);
        return trans.get_response_status();
    }

    void write(uint32_t reg, uint32_t value)
    {
        ASSERT_EQ(access(tlm::TLM_WRITE_COMMAND, reg, value), tlm::TLM_OK_RESPONSE);
    }

    uint32_t read(uint32_t reg)
    {
        uint32_t value = 0;
        EXPECT_EQ(access(tlm::TLM_READ_COMMAND, reg, value), tlm::TLM_OK_RESPONSE);
        return value;
    }

    void init_linux_style()
    {
        write(dw_apb_i2c::IC_ENABLE, 0);
        write(dw_apb_i2c::IC_SS_SCL_HCNT, 400);
        write(dw_apb_i2c::IC_SS_SCL_LCNT, 470);
        write(dw_apb_i2c::IC_FS_SCL_HCNT, 60);
        write(dw_apb_i2c::IC_FS_SCL_LCNT, 130);
        write(dw_apb_i2c::IC_RX_TL, 0);
        write(dw_apb_i2c::IC_TX_TL, 8);
        write(dw_apb_i2c::IC_CON, 1 | (2 << 1) | (1 << 5) | (1 << 6) | (1 << 8));
        write(dw_apb_i2c::IC_TAR, 0x50);
        write(dw_apb_i2c::IC_ENABLE, 1);
        write(dw_apb_i2c::IC_CLR_INTR, 0);
        write(dw_apb_i2c::IC_INTR_MASK, dw_apb_i2c::INTR_RX_FULL | dw_apb_i2c::INTR_TX_ABRT |
                                            dw_apb_i2c::INTR_STOP_DET | dw_apb_i2c::INTR_TX_EMPTY);
    }
};

TEST_BENCH(DwApbI2cBench, DmaRequestsAndDataWidths)
{
    constexpr uint32_t ACTIVE = gs::dma_trigger_request::ACTIVE;
    init_linux_style();
    write(dw_apb_i2c::IC_DMA_TDLR, 0);
    write(dw_apb_i2c::IC_DMA_CR, dw_apb_i2c::DMA_TDMAE);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_EQ(dma_tx_req.read(), ACTIVE);

    controller.dma_tx_ack->write(ACTIVE);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_EQ(dma_tx_req.read(), 0U);

    uint32_t command = 0x20;
    EXPECT_EQ(access(tlm::TLM_WRITE_COMMAND, dw_apb_i2c::IC_DATA_CMD, command, 2, 2), tlm::TLM_OK_RESPONSE);
    command = 0x5a | dw_apb_i2c::DATA_CMD_STOP;
    EXPECT_EQ(access(tlm::TLM_WRITE_COMMAND, dw_apb_i2c::IC_DATA_CMD, command, 2, 2), tlm::TLM_OK_RESPONSE);
    controller.dma_tx_ack->write(0);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_EQ(dma_tx_req.read(), 0U);

    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(dma_tx_req.read(), ACTIVE);
    controller.dma_tx_ack->write(ACTIVE);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    write(dw_apb_i2c::IC_DMA_CR, 0);
    controller.dma_tx_ack->write(0);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_EQ(dma_tx_req.read(), 0U);

    write(dw_apb_i2c::IC_DMA_RDLR, 0);
    write(dw_apb_i2c::IC_DMA_CR, dw_apb_i2c::DMA_RDMAE);
    command = 0x20;
    EXPECT_EQ(access(tlm::TLM_WRITE_COMMAND, dw_apb_i2c::IC_DATA_CMD, command, 2, 2), tlm::TLM_OK_RESPONSE);
    command = dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP;
    EXPECT_EQ(access(tlm::TLM_WRITE_COMMAND, dw_apb_i2c::IC_DATA_CMD, command, 2, 2), tlm::TLM_OK_RESPONSE);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(dma_rx_req.read(), ACTIVE);

    controller.dma_rx_ack->write(ACTIVE);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_EQ(dma_rx_req.read(), 0U);
    uint32_t data = 0;
    EXPECT_EQ(access(tlm::TLM_READ_COMMAND, dw_apb_i2c::IC_DATA_CMD, data, 1, 1), tlm::TLM_OK_RESPONSE);
    EXPECT_EQ(data, 0x5aU);
    controller.dma_rx_ack->write(0);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_EQ(dma_rx_req.read(), 0U);

    controller.reset->write(true);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_EQ(dma_tx_req.read(), 0U);
    EXPECT_EQ(dma_rx_req.read(), 0U);
}

TEST_BENCH(DwApbI2cBench, LinuxInitAndEepromRepeatedStart)
{
    EXPECT_EQ(read(dw_apb_i2c::IC_COMP_TYPE), dw_apb_i2c::COMP_TYPE);
    EXPECT_EQ(((read(dw_apb_i2c::IC_COMP_PARAM_1) >> 16) & 0xff) + 1, dw_apb_i2c::FIFO_DEPTH);
    init_linux_style();
    EXPECT_TRUE(irq.read());

    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, 0xa5 | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_NE(read(dw_apb_i2c::IC_RAW_INTR_STAT) & dw_apb_i2c::INTR_STOP_DET, 0U);
    read(dw_apb_i2c::IC_CLR_STOP_DET);

    controller.reset->write(true);
    eeprom.reset->write(true);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_FALSE(irq.read());
    controller.reset->write(false);
    eeprom.reset->write(false);
    init_linux_style();

    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD,
          dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_RXFLR), 1U);
    EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), 0xa5U);
    EXPECT_EQ(read(dw_apb_i2c::IC_RXFLR), 0U);
}

TEST_BENCH(DwApbI2cBench, InterruptAbortAndReset)
{
    init_linux_style();
    write(dw_apb_i2c::IC_TAR, 0x53);
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(15, sc_core::SC_US));
    EXPECT_TRUE(irq.read());
    EXPECT_NE(read(dw_apb_i2c::IC_RAW_INTR_STAT) & dw_apb_i2c::INTR_TX_ABRT, 0U);
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 1U);
    read(dw_apb_i2c::IC_CLR_TX_ABRT);
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 0U);

    controller.reset->write(true);
    eeprom.reset->write(true);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_FALSE(irq.read());
    EXPECT_EQ(read(dw_apb_i2c::IC_ENABLE_STATUS), 0U);
    EXPECT_EQ(read(dw_apb_i2c::IC_RAW_INTR_STAT), 0U);
}

TEST_BENCH(DwApbI2cBench, PinmuxDisableAndRecover)
{
    init_linux_style();
    controller.pinmux_enable->write(false);
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(15, sc_core::SC_US));

    EXPECT_NE(read(dw_apb_i2c::IC_RAW_INTR_STAT) & dw_apb_i2c::INTR_TX_ABRT, 0U);
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 1U);
    EXPECT_EQ(read(dw_apb_i2c::IC_TXFLR), 0U);
    read(dw_apb_i2c::IC_CLR_TX_ABRT);

    controller.pinmux_enable->write(true);
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, 0x5a | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    read(dw_apb_i2c::IC_CLR_STOP_DET);

    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD,
          dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_RXFLR), 1U);
    EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), 0x5aU);
}

TEST_BENCH(DwApbI2cBench, RejectsMalformedTransactions)
{
    uint32_t value = 0;
    EXPECT_EQ(access(tlm::TLM_READ_COMMAND, 2, value), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    EXPECT_EQ(access(tlm::TLM_READ_COMMAND, dw_apb_i2c::IC_CON, value, 2, 2), tlm::TLM_BURST_ERROR_RESPONSE);
    EXPECT_EQ(access(tlm::TLM_READ_COMMAND, dw_apb_i2c::IC_CON, value, 4, 2), tlm::TLM_BURST_ERROR_RESPONSE);
    unsigned char byte_enable = TLM_BYTE_ENABLED;
    EXPECT_EQ(access(tlm::TLM_READ_COMMAND, dw_apb_i2c::IC_CON, value, 4, 4, &byte_enable),
              tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
}

TEST_BENCH(DwApbI2cBench, ThreeTargetsPageWrapAndSixteenBitAddress)
{
    init_linux_style();
    for (uint32_t address = 0x50; address <= 0x52; ++address) {
        write(dw_apb_i2c::IC_TAR, address);
        if (address == 0x52) write(dw_apb_i2c::IC_DATA_CMD, 1);
        write(dw_apb_i2c::IC_DATA_CMD, address == 0x52 ? 63 : 7);
        write(dw_apb_i2c::IC_DATA_CMD, address);
        write(dw_apb_i2c::IC_DATA_CMD, (address + 0x20) | dw_apb_i2c::DATA_CMD_STOP);
        sc_core::wait(sc_core::sc_time(50, sc_core::SC_US));
    }
    for (uint32_t address = 0x50; address <= 0x52; ++address) {
        write(dw_apb_i2c::IC_TAR, address);
        if (address == 0x52) write(dw_apb_i2c::IC_DATA_CMD, 1);
        write(dw_apb_i2c::IC_DATA_CMD, 0);
        write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ |
              dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
        sc_core::wait(sc_core::sc_time(40, sc_core::SC_US));
        EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), address + 0x20);
        EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 0U);
    }
}

TEST_BENCH(DwApbI2cBench, SixteenBitAddressDoesNotAliasLowPage)
{
    init_linux_style();
    write(dw_apb_i2c::IC_TAR, 0x52);
    write(dw_apb_i2c::IC_DATA_CMD, 1);
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, 0x5a | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(40, sc_core::SC_US));
    for (unsigned high = 0; high < 2; ++high) {
        write(dw_apb_i2c::IC_DATA_CMD, high);
        write(dw_apb_i2c::IC_DATA_CMD, 0x20);
        write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ |
              dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
        sc_core::wait(sc_core::sc_time(40, sc_core::SC_US));
        EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), high ? 0x5aU : 0xffU);
    }
}

TEST_BENCH(DwApbI2cBench, ControllerResetCancelsUncommittedPage)
{
    init_linux_style();
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, 0xa5); // No STOP: still in the target page buffer.
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    controller.reset->write(true);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    controller.reset->write(false);
    init_linux_style();
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ |
          dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), 0xffU);
}

TEST_BENCH(DwApbI2cBench, BusyTargetDoesNotBlockOtherAddress)
{
    eeprom.p_write_cycle.set_value(sc_core::sc_time(200, sc_core::SC_US));
    ASSERT_EQ(eeprom.p_write_cycle.get_value(), sc_core::sc_time(200, sc_core::SC_US));
    init_linux_style();
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, 0x5a | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(15, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 1U);
    read(dw_apb_i2c::IC_CLR_TX_ABRT);
    write(dw_apb_i2c::IC_TAR, 0x51);
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(15, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 0U);
    EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), 0xffU);
    sc_core::wait(sc_core::sc_time(200, sc_core::SC_US));
    write(dw_apb_i2c::IC_TAR, 0x50);
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ |
          dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), 0x5aU);
}

TEST_BENCH(DwApbI2cBench, AbortHoldsFifoUntilAcknowledged)
{
    eeprom.p_write_cycle.set_value(sc_core::sc_time(200, sc_core::SC_US));
    init_linux_style();
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, 0x5a | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    read(dw_apb_i2c::IC_CLR_STOP_DET);
    write(dw_apb_i2c::IC_DATA_CMD, 0x28);
    sc_core::wait(sc_core::sc_time(15, sc_core::SC_US));
    ASSERT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 1U);
    observer.events.clear();

    // Simulate a preempted FIFO refill resuming after the target exits busy.
    sc_core::wait(sc_core::sc_time(200, sc_core::SC_US));
    write(dw_apb_i2c::IC_DATA_CMD, 0x79);
    write(dw_apb_i2c::IC_DATA_CMD, 0x2c | dw_apb_i2c::DATA_CMD_STOP);
    EXPECT_EQ(read(dw_apb_i2c::IC_TXFLR), 0U);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_TRUE(observer.events.empty());
    EXPECT_EQ(read(dw_apb_i2c::IC_RAW_INTR_STAT) & dw_apb_i2c::INTR_STOP_DET, 0U);
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 1U);

    read(dw_apb_i2c::IC_CLR_TX_ABRT);
    write(dw_apb_i2c::IC_DATA_CMD, 0x79);
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ |
          dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), 0xffU);
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 0U);
}

TEST_BENCH(DwApbI2cBench, DelayedReadResponseAndRestartBoundary)
{
    init_linux_style();
    write(dw_apb_i2c::IC_TAR, 0x54);
    observer.events.clear();
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ);
    sc_core::wait(sc_core::sc_time(20, sc_core::SC_US));
    EXPECT_EQ(observer.events, (std::vector<std::string>{"start", "address", "read"}));
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ);
    sc_core::wait(sc_core::sc_time(20, sc_core::SC_US));
    EXPECT_EQ(observer.events.back(), "read");
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ |
          dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(20, sc_core::SC_US));
    EXPECT_EQ(observer.events, (std::vector<std::string>{
        "start", "address", "read", "ack", "read", "nack", "restart", "address", "read", "nack", "stop"}));
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 0U);
    EXPECT_EQ(read(dw_apb_i2c::IC_RXFLR), 3U);
}

TEST_BENCH(DwApbI2cBench, StopReachesPreviousTargetAfterAddressNack)
{
    init_linux_style();
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, 0x5a);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    write(dw_apb_i2c::IC_TAR, 0x53);
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ |
          dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(15, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 1U);
    read(dw_apb_i2c::IC_CLR_TX_ABRT);
    write(dw_apb_i2c::IC_TAR, 0x50);
    write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ |
          dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(read(dw_apb_i2c::IC_DATA_CMD), 0x5aU);
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker{};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
