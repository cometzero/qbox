/* SPDX-License-Identifier: BSD-3-Clause */

#include <cstdint>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <cci/utils/broker.h>
#include <tests/test-bench.h>

#include "dw-apb-i2c.h"
#include "i2c-bus.h"
#include "pca9539.h"

class Pca9539Bench : public TestBench
{
protected:
    tlm_utils::simple_initiator_socket<Pca9539Bench> cpu_socket;
    dw_apb_i2c controller;
    i2c_bus bus;
    dw_i2c_eeprom eeprom;
    pca9539 expander;
    sc_core::sc_signal<bool> controller_irq;
    sc_core::sc_signal<bool> int_n;
    sc_core::sc_signal<bool> oe_0;
    sc_core::sc_signal<bool> oe_8;
    sc_core::sc_signal<bool> effective_2;

    explicit Pca9539Bench(const sc_core::sc_module_name& name)
        : TestBench(name)
        , cpu_socket("cpu_socket")
        , controller("controller")
        , bus("bus")
        , eeprom("eeprom")
        , expander("expander")
        , controller_irq("controller_irq")
        , int_n("int_n")
        , oe_0("oe_0")
        , oe_8("oe_8")
        , effective_2("effective_2")
    {
        cpu_socket.bind(controller.target_socket);
        controller.i2c_socket.bind(bus.target_socket);
        bus.initiator_socket.bind(eeprom.i2c_socket);
        bus.initiator_socket.bind(expander.i2c_socket);
        controller.irq.bind(controller_irq);
        expander.int_n.bind(int_n);
        expander.gpio_out[0].bind(expander.gpio_in[1]);
        expander.gpio_out[8].bind(expander.gpio_in[9]);
        expander.gpio_oe[0].bind(oe_0);
        expander.gpio_oe[8].bind(oe_8);
        expander.gpio_out[2].bind(effective_2);
    }

    tlm::tlm_response_status access(tlm::tlm_command command, uint64_t address, uint32_t& value)
    {
        tlm::tlm_generic_payload trans;
        trans.set_command(command);
        trans.set_address(address);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_ptr(nullptr);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        cpu_socket->b_transport(trans, delay);
        sc_core::wait(delay);
        return trans.get_response_status();
    }

    void mmio_write(uint32_t reg, uint32_t value)
    {
        ASSERT_EQ(access(tlm::TLM_WRITE_COMMAND, reg, value), tlm::TLM_OK_RESPONSE);
    }

    uint32_t mmio_read(uint32_t reg)
    {
        uint32_t value = 0;
        EXPECT_EQ(access(tlm::TLM_READ_COMMAND, reg, value), tlm::TLM_OK_RESPONSE);
        return value;
    }

    void select_target(uint8_t address)
    {
        mmio_write(dw_apb_i2c::IC_ENABLE, 0);
        mmio_write(dw_apb_i2c::IC_TAR, address);
        mmio_write(dw_apb_i2c::IC_ENABLE, 1);
        mmio_read(dw_apb_i2c::IC_CLR_INTR);
    }

    void write_register_pair(uint8_t reg, uint8_t low, uint8_t high)
    {
        select_target(0x74);
        mmio_write(dw_apb_i2c::IC_DATA_CMD, reg);
        mmio_write(dw_apb_i2c::IC_DATA_CMD, low);
        mmio_write(dw_apb_i2c::IC_DATA_CMD, high | dw_apb_i2c::DATA_CMD_STOP);
        sc_core::wait(sc_core::sc_time(35, sc_core::SC_US));
        ASSERT_EQ(mmio_read(dw_apb_i2c::IC_RAW_INTR_STAT) & dw_apb_i2c::INTR_TX_ABRT, 0U);
    }

    uint16_t read_register_pair(uint8_t reg)
    {
        select_target(0x74);
        mmio_write(dw_apb_i2c::IC_DATA_CMD, reg);
        mmio_write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_RESTART);
        mmio_write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_STOP);
        sc_core::wait(sc_core::sc_time(35, sc_core::SC_US));
        EXPECT_EQ(mmio_read(dw_apb_i2c::IC_RXFLR), 2U);
        const uint16_t low = mmio_read(dw_apb_i2c::IC_DATA_CMD);
        const uint16_t high = mmio_read(dw_apb_i2c::IC_DATA_CMD);
        return low | (high << 8);
    }

    uint8_t read_register_byte(uint8_t reg)
    {
        select_target(0x74);
        mmio_write(dw_apb_i2c::IC_DATA_CMD, reg);
        mmio_write(dw_apb_i2c::IC_DATA_CMD,
                   dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_RESTART | dw_apb_i2c::DATA_CMD_STOP);
        sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
        EXPECT_EQ(mmio_read(dw_apb_i2c::IC_RXFLR), 1U);
        return mmio_read(dw_apb_i2c::IC_DATA_CMD);
    }

    void release_reset()
    {
        expander.reset_n->write(true);
        mmio_write(dw_apb_i2c::IC_ENABLE, 0);
        mmio_write(dw_apb_i2c::IC_CON, 1 | (2 << 1) | (1 << 5) | (1 << 6) | (1 << 8));
        sc_core::wait(sc_core::SC_ZERO_TIME);
    }
};

TEST_BENCH(Pca9539Bench, SharedBusRoutesByAddress)
{
    release_reset();

    select_target(0x50);
    mmio_write(dw_apb_i2c::IC_DATA_CMD, 0x20);
    mmio_write(dw_apb_i2c::IC_DATA_CMD, 0xa5 | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(25, sc_core::SC_US));
    EXPECT_EQ(mmio_read(dw_apb_i2c::IC_RAW_INTR_STAT) & dw_apb_i2c::INTR_TX_ABRT, 0U);

    EXPECT_EQ(read_register_pair(pca9539::CONFIG_PORT_0), 0xffffU);

    select_target(0x60);
    mmio_write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(15, sc_core::SC_US));
    EXPECT_NE(mmio_read(dw_apb_i2c::IC_RAW_INTR_STAT) & dw_apb_i2c::INTR_TX_ABRT, 0U);
    EXPECT_EQ(mmio_read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 1U);
}

TEST_BENCH(Pca9539Bench, DirectionPolarityAndInterrupts)
{
    release_reset();
    EXPECT_TRUE(int_n.read());

    write_register_pair(pca9539::OUTPUT_PORT_0, 0xff, 0xff);
    write_register_pair(pca9539::CONFIG_PORT_0, 0xfe, 0xfe);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_TRUE(oe_0.read());
    EXPECT_TRUE(oe_8.read());
    EXPECT_FALSE(int_n.read());

    EXPECT_EQ(read_register_byte(pca9539::INPUT_PORT_0) & 0x02U, 0x02U);
    EXPECT_FALSE(int_n.read());
    EXPECT_EQ(read_register_byte(pca9539::INPUT_PORT_1) & 0x02U, 0x02U);
    EXPECT_TRUE(int_n.read());

    write_register_pair(pca9539::OUTPUT_PORT_0, 0xfe, 0xfe);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_FALSE(int_n.read());
    EXPECT_EQ(read_register_pair(pca9539::INPUT_PORT_0) & 0x0202U, 0U);
    EXPECT_TRUE(int_n.read());

    write_register_pair(pca9539::POLARITY_PORT_0, 0x02, 0x02);
    EXPECT_EQ(read_register_pair(pca9539::INPUT_PORT_0) & 0x0202U, 0x0202U);
}

TEST_BENCH(Pca9539Bench, ResetHoldsBusAndRestoresDefaults)
{
    release_reset();
    expander.gpio_in[2]->write(true);
    sc_core::wait(sc_core::sc_time(1, sc_core::SC_NS));
    EXPECT_TRUE(effective_2.read());
    read_register_byte(pca9539::INPUT_PORT_0);
    write_register_pair(pca9539::CONFIG_PORT_0, 0xfe, 0xfe);
    EXPECT_TRUE(oe_0.read());

    expander.reset_n->write(false);
    sc_core::wait(sc_core::sc_time(1, sc_core::SC_NS));
    EXPECT_FALSE(oe_0.read());
    EXPECT_TRUE(int_n.read());

    select_target(0x74);
    mmio_write(dw_apb_i2c::IC_DATA_CMD, dw_apb_i2c::DATA_CMD_READ | dw_apb_i2c::DATA_CMD_STOP);
    sc_core::wait(sc_core::sc_time(15, sc_core::SC_US));
    EXPECT_NE(mmio_read(dw_apb_i2c::IC_RAW_INTR_STAT) & dw_apb_i2c::INTR_TX_ABRT, 0U);
    EXPECT_EQ(mmio_read(dw_apb_i2c::IC_TX_ABRT_SOURCE), 1U);

    release_reset();
    EXPECT_EQ(read_register_pair(pca9539::OUTPUT_PORT_0), 0xffffU);
    EXPECT_EQ(read_register_pair(pca9539::POLARITY_PORT_0), 0x0000U);
    EXPECT_EQ(read_register_pair(pca9539::CONFIG_PORT_0), 0xffffU);
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker{};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
