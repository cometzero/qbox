/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <cci/utils/broker.h>
#include <tests/test-bench.h>

#include "dw-apb-i2c.h"

class DwApbI2cBench : public TestBench
{
protected:
    tlm_utils::simple_initiator_socket<DwApbI2cBench> cpu_socket;
    dw_apb_i2c controller;
    dw_i2c_eeprom eeprom;
    sc_core::sc_signal<bool> irq;

    explicit DwApbI2cBench(const sc_core::sc_module_name& name)
        : TestBench(name), cpu_socket("cpu_socket"), controller("controller"), eeprom("eeprom"), irq("irq")
    {
        cpu_socket.bind(controller.target_socket);
        controller.i2c_socket.bind(eeprom.i2c_socket);
        controller.irq.bind(irq);
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
    write(dw_apb_i2c::IC_TAR, 0x51);
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

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker{};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
