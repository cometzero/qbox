/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QBOX_DW_APB_I2C_H
#define QBOX_DW_APB_I2C_H

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <async_event.h>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <scp/report.h>
#include <tlm_sockets_buswidth.h>
#include <i2c-transaction.h>

class dw_apb_i2c : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    enum Register : uint32_t {
        IC_CON = 0x00,
        IC_TAR = 0x04,
        IC_SAR = 0x08,
        IC_DATA_CMD = 0x10,
        IC_SS_SCL_HCNT = 0x14,
        IC_SS_SCL_LCNT = 0x18,
        IC_FS_SCL_HCNT = 0x1c,
        IC_FS_SCL_LCNT = 0x20,
        IC_HS_SCL_HCNT = 0x24,
        IC_HS_SCL_LCNT = 0x28,
        IC_INTR_STAT = 0x2c,
        IC_INTR_MASK = 0x30,
        IC_RAW_INTR_STAT = 0x34,
        IC_RX_TL = 0x38,
        IC_TX_TL = 0x3c,
        IC_CLR_INTR = 0x40,
        IC_CLR_RX_UNDER = 0x44,
        IC_CLR_RX_OVER = 0x48,
        IC_CLR_TX_OVER = 0x4c,
        IC_CLR_RD_REQ = 0x50,
        IC_CLR_TX_ABRT = 0x54,
        IC_CLR_RX_DONE = 0x58,
        IC_CLR_ACTIVITY = 0x5c,
        IC_CLR_STOP_DET = 0x60,
        IC_CLR_START_DET = 0x64,
        IC_CLR_GEN_CALL = 0x68,
        IC_ENABLE = 0x6c,
        IC_STATUS = 0x70,
        IC_TXFLR = 0x74,
        IC_RXFLR = 0x78,
        IC_SDA_HOLD = 0x7c,
        IC_TX_ABRT_SOURCE = 0x80,
        IC_ENABLE_STATUS = 0x9c,
        IC_CLR_RESTART_DET = 0xa8,
        IC_SMBUS_INTR_MASK = 0xcc,
        IC_COMP_PARAM_1 = 0xf4,
        IC_COMP_VERSION = 0xf8,
        IC_COMP_TYPE = 0xfc,
    };

    enum Interrupt : uint32_t {
        INTR_RX_UNDER = 1U << 0,
        INTR_RX_OVER = 1U << 1,
        INTR_RX_FULL = 1U << 2,
        INTR_TX_OVER = 1U << 3,
        INTR_TX_EMPTY = 1U << 4,
        INTR_RD_REQ = 1U << 5,
        INTR_TX_ABRT = 1U << 6,
        INTR_RX_DONE = 1U << 7,
        INTR_ACTIVITY = 1U << 8,
        INTR_STOP_DET = 1U << 9,
        INTR_START_DET = 1U << 10,
        INTR_GEN_CALL = 1U << 11,
        INTR_RESTART_DET = 1U << 12,
        INTR_MST_ON_HOLD = 1U << 13,
    };

    static constexpr uint32_t DATA_CMD_READ = 1U << 8;
    static constexpr uint32_t DATA_CMD_STOP = 1U << 9;
    static constexpr uint32_t DATA_CMD_RESTART = 1U << 10;
    static constexpr uint32_t COMP_TYPE = 0x44570140;
    static constexpr unsigned FIFO_DEPTH = 16;

    tlm_utils::simple_target_socket<dw_apb_i2c, DEFAULT_TLM_BUSWIDTH> target_socket;
    InitiatorSignalSocket<bool> irq;
    tlm_utils::simple_initiator_socket<dw_apb_i2c, DEFAULT_TLM_BUSWIDTH> i2c_socket;
    TargetSignalSocket<bool> reset;

    cci::cci_param<sc_core::sc_time> p_access_latency;
    cci::cci_param<sc_core::sc_time> p_transfer_latency;

    SC_HAS_PROCESS(dw_apb_i2c);
    explicit dw_apb_i2c(sc_core::sc_module_name name);

    void before_end_of_elaboration() override;

private:
    static constexpr uint32_t ENABLE = 1U << 0;
    static constexpr uint32_t ENABLE_ABORT = 1U << 1;
    static constexpr uint32_t ABRT_7B_ADDR_NOACK = 1U << 0;
    static constexpr uint32_t ABRT_TXDATA_NOACK = 1U << 3;
    static constexpr uint32_t ABRT_MASTER_DIS = 1U << 11;
    static constexpr uint32_t STATUS_ACTIVITY = 1U << 0;
    static constexpr uint32_t STATUS_TFNF = 1U << 1;
    static constexpr uint32_t STATUS_TFE = 1U << 2;
    static constexpr uint32_t STATUS_RFNE = 1U << 3;
    static constexpr uint32_t STATUS_RFF = 1U << 4;
    static constexpr uint32_t STATUS_MASTER_ACTIVITY = 1U << 5;

    std::array<uint32_t, 64> m_registers{};
    std::deque<uint16_t> m_tx_fifo;
    std::deque<uint8_t> m_rx_fifo;
    uint32_t m_latched_interrupts = 0;
    uint32_t m_abort_source = 0;
    bool m_active = false;
    bool m_irq_level = false;
    uint64_t m_reset_generation = 0;
    gs::async_event m_command_event;
    gs::async_event m_reset_event;
    gs::async_event m_irq_event;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    uint32_t read_register(uint32_t offset);
    void write_register(uint32_t offset, uint32_t value);
    uint32_t raw_interrupts() const;
    uint32_t status() const;
    void update_irq();
    void drive_irq();
    void reset_controller();
    void transfer_thread();
    void execute_command(uint16_t command);
    void abort_transfer(uint32_t source);
};

class dw_i2c_eeprom : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    cci::cci_param<uint32_t> p_address;
    cci::cci_param<uint32_t> p_size;
    cci::cci_param<uint32_t> p_address_width;
    cci::cci_param<uint32_t> p_page_size;
    cci::cci_param<sc_core::sc_time> p_access_latency;

    tlm_utils::simple_target_socket<dw_i2c_eeprom, DEFAULT_TLM_BUSWIDTH> i2c_socket;
    TargetSignalSocket<bool> reset;

    explicit dw_i2c_eeprom(sc_core::sc_module_name name);

private:
    std::vector<uint8_t> m_storage;
    uint32_t m_pointer = 0;
    uint32_t m_page_base = 0;
    bool m_transaction_active = false;
    bool m_expect_address = true;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    void reset_transaction();
    void advance_write_pointer();
};

extern "C" void module_register();

#endif
