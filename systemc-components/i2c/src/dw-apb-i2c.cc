/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "dw-apb-i2c.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint32_t VALID_INTERRUPTS = (1U << 14) - 1;
constexpr uint32_t COMP_VERSION = 0x3132312a; // "1.21*"
constexpr uint32_t COMP_PARAM_1 = ((dw_apb_i2c::FIFO_DEPTH - 1) << 16) | ((dw_apb_i2c::FIFO_DEPTH - 1) << 8) |
                                  (1U << 7) | (2U << 2) | 2U;
} // namespace

dw_apb_i2c::dw_apb_i2c(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , target_socket("target_socket")
    , irq("irq")
    , i2c_socket("i2c_socket")
    , reset("reset")
    , p_access_latency("access_latency", sc_core::sc_time(10, sc_core::SC_NS), "MMIO access latency")
    , p_transfer_latency("transfer_latency", sc_core::sc_time(10, sc_core::SC_US), "I2C byte transfer latency")
    , m_command_event(false)
    , m_reset_event(false)
    , m_irq_event(false)
{
    target_socket.register_b_transport(this, &dw_apb_i2c::b_transport);
    reset.register_value_changed_cb([this](bool asserted) {
        if (asserted) {
            reset_controller();
            m_reset_event.notify();
            update_irq();
        }
    });

    reset_controller();
    SC_THREAD(transfer_thread);
    SC_METHOD(drive_irq);
    sensitive << m_irq_event;
}

void dw_apb_i2c::before_end_of_elaboration()
{
    if (!irq.get_interface()) {
        auto* stub = new sc_core::sc_signal<bool>(sc_core::sc_gen_unique_name("irq_stub"));
        irq.bind(*stub);
    }
}

void dw_apb_i2c::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    trans.set_dmi_allowed(false);

    if (!trans.get_data_ptr()) {
        trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
    }
    if (trans.get_byte_enable_ptr()) {
        trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        return;
    }
    if (trans.get_data_length() != sizeof(uint32_t)) {
        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }
    if (trans.get_streaming_width() < trans.get_data_length()) {
        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }
    if ((trans.get_address() & 3U) || trans.get_address() > IC_COMP_TYPE) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    uint32_t value = 0;
    switch (trans.get_command()) {
    case tlm::TLM_READ_COMMAND:
        value = read_register(static_cast<uint32_t>(trans.get_address()));
        std::memcpy(trans.get_data_ptr(), &value, sizeof(value));
        break;
    case tlm::TLM_WRITE_COMMAND:
        std::memcpy(&value, trans.get_data_ptr(), sizeof(value));
        write_register(static_cast<uint32_t>(trans.get_address()), value);
        break;
    default:
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    delay += p_access_latency.get_value();
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

uint32_t dw_apb_i2c::raw_interrupts() const
{
    uint32_t raw = m_latched_interrupts;
    if ((m_registers[IC_ENABLE / 4] & ENABLE) && m_tx_fifo.size() <= m_registers[IC_TX_TL / 4]) {
        raw |= INTR_TX_EMPTY;
    }
    if (m_rx_fifo.size() > m_registers[IC_RX_TL / 4]) {
        raw |= INTR_RX_FULL;
    }
    return raw & VALID_INTERRUPTS;
}

uint32_t dw_apb_i2c::status() const
{
    uint32_t value = 0;
    if (m_active) value |= STATUS_ACTIVITY | STATUS_MASTER_ACTIVITY;
    if (m_tx_fifo.size() < FIFO_DEPTH) value |= STATUS_TFNF;
    if (m_tx_fifo.empty()) value |= STATUS_TFE;
    if (!m_rx_fifo.empty()) value |= STATUS_RFNE;
    if (m_rx_fifo.size() == FIFO_DEPTH) value |= STATUS_RFF;
    return value;
}

uint32_t dw_apb_i2c::read_register(uint32_t offset)
{
    uint32_t value = 0;
    switch (offset) {
    case IC_DATA_CMD:
        if (m_rx_fifo.empty()) {
            m_latched_interrupts |= INTR_RX_UNDER;
        } else {
            value = m_rx_fifo.front();
            m_rx_fifo.pop_front();
        }
        break;
    case IC_INTR_STAT:
        value = raw_interrupts() & m_registers[IC_INTR_MASK / 4];
        break;
    case IC_RAW_INTR_STAT:
        value = raw_interrupts();
        break;
    case IC_CLR_INTR:
        value = raw_interrupts();
        m_latched_interrupts = 0;
        m_abort_source = 0;
        break;
    case IC_CLR_RX_UNDER:
        value = m_latched_interrupts & INTR_RX_UNDER;
        m_latched_interrupts &= ~INTR_RX_UNDER;
        break;
    case IC_CLR_RX_OVER:
        value = m_latched_interrupts & INTR_RX_OVER;
        m_latched_interrupts &= ~INTR_RX_OVER;
        break;
    case IC_CLR_TX_OVER:
        value = m_latched_interrupts & INTR_TX_OVER;
        m_latched_interrupts &= ~INTR_TX_OVER;
        break;
    case IC_CLR_TX_ABRT:
        value = m_latched_interrupts & INTR_TX_ABRT;
        m_latched_interrupts &= ~INTR_TX_ABRT;
        m_abort_source = 0;
        break;
    case IC_CLR_STOP_DET:
        value = m_latched_interrupts & INTR_STOP_DET;
        m_latched_interrupts &= ~INTR_STOP_DET;
        break;
    case IC_CLR_RD_REQ:
    case IC_CLR_RX_DONE:
    case IC_CLR_ACTIVITY:
    case IC_CLR_START_DET:
    case IC_CLR_GEN_CALL:
    case IC_CLR_RESTART_DET: {
        const uint32_t bit = offset == IC_CLR_RD_REQ      ? INTR_RD_REQ
                             : offset == IC_CLR_RX_DONE   ? INTR_RX_DONE
                             : offset == IC_CLR_ACTIVITY  ? INTR_ACTIVITY
                             : offset == IC_CLR_START_DET ? INTR_START_DET
                             : offset == IC_CLR_GEN_CALL  ? INTR_GEN_CALL
                                                          : INTR_RESTART_DET;
        value = m_latched_interrupts & bit;
        m_latched_interrupts &= ~bit;
        break;
    }
    case IC_ENABLE_STATUS:
        value = m_registers[IC_ENABLE / 4] & ENABLE;
        break;
    case IC_STATUS:
        value = status();
        break;
    case IC_TXFLR:
        value = static_cast<uint32_t>(m_tx_fifo.size());
        break;
    case IC_RXFLR:
        value = static_cast<uint32_t>(m_rx_fifo.size());
        break;
    case IC_TX_ABRT_SOURCE:
        value = m_abort_source;
        break;
    case IC_COMP_PARAM_1:
        value = COMP_PARAM_1;
        break;
    case IC_COMP_VERSION:
        value = COMP_VERSION;
        break;
    case IC_COMP_TYPE:
        value = COMP_TYPE;
        break;
    default:
        value = m_registers[offset / 4];
        break;
    }
    update_irq();
    return value;
}

void dw_apb_i2c::write_register(uint32_t offset, uint32_t value)
{
    switch (offset) {
    case IC_CON:
    case IC_TAR:
    case IC_SAR:
    case IC_SS_SCL_HCNT:
    case IC_SS_SCL_LCNT:
    case IC_FS_SCL_HCNT:
    case IC_FS_SCL_LCNT:
    case IC_HS_SCL_HCNT:
    case IC_HS_SCL_LCNT:
    case IC_SDA_HOLD:
    case IC_SMBUS_INTR_MASK:
        m_registers[offset / 4] = value;
        break;
    case IC_INTR_MASK:
        m_registers[offset / 4] = value & VALID_INTERRUPTS;
        break;
    case IC_RX_TL:
    case IC_TX_TL:
        m_registers[offset / 4] = std::min<uint32_t>(value, FIFO_DEPTH - 1);
        break;
    case IC_ENABLE:
        if (value & ENABLE_ABORT) {
            abort_transfer(ABRT_MASTER_DIS);
        }
        m_registers[offset / 4] = value & ENABLE;
        if (!(value & ENABLE)) {
            m_tx_fifo.clear();
            m_rx_fifo.clear();
            m_active = false;
        }
        break;
    case IC_DATA_CMD:
        if (!(m_registers[IC_ENABLE / 4] & ENABLE)) {
            abort_transfer(ABRT_MASTER_DIS);
        } else if (m_tx_fifo.size() == FIFO_DEPTH) {
            m_latched_interrupts |= INTR_TX_OVER;
        } else {
            m_tx_fifo.push_back(static_cast<uint16_t>(value));
            m_active = true;
            m_command_event.notify();
        }
        break;
    default:
        break;
    }
    update_irq();
}

void dw_apb_i2c::update_irq()
{
    const bool level = (raw_interrupts() & m_registers[IC_INTR_MASK / 4]) != 0;
    if (level != m_irq_level) {
        m_irq_level = level;
        m_irq_event.notify();
    }
}

void dw_apb_i2c::drive_irq() { irq->write(m_irq_level); }

void dw_apb_i2c::reset_controller()
{
    ++m_reset_generation;
    m_registers.fill(0);
    m_registers[IC_CON / 4] = 0x7f;
    m_registers[IC_SS_SCL_HCNT / 4] = 0x190;
    m_registers[IC_SS_SCL_LCNT / 4] = 0x1d6;
    m_registers[IC_FS_SCL_HCNT / 4] = 0x3c;
    m_registers[IC_FS_SCL_LCNT / 4] = 0x82;
    m_tx_fifo.clear();
    m_rx_fifo.clear();
    m_latched_interrupts = 0;
    m_abort_source = 0;
    m_active = false;
}

void dw_apb_i2c::abort_transfer(uint32_t source)
{
    m_abort_source |= source;
    m_latched_interrupts |= INTR_TX_ABRT;
    m_tx_fifo.clear();
    m_rx_fifo.clear();
    m_active = false;
}

void dw_apb_i2c::transfer_thread()
{
    while (true) {
        while (m_tx_fifo.empty()) sc_core::wait(m_command_event);

        const uint16_t command = m_tx_fifo.front();
        m_tx_fifo.pop_front();
        const uint64_t generation = m_reset_generation;
        update_irq();

        sc_core::wait(p_transfer_latency.get_value(), m_reset_event);
        if (reset.read() || generation != m_reset_generation) continue;
        execute_command(command);
    }
}

void dw_apb_i2c::execute_command(uint16_t command)
{
    const uint64_t generation = m_reset_generation;
    if (!(m_registers[IC_ENABLE / 4] & ENABLE)) {
        abort_transfer(ABRT_MASTER_DIS);
        update_irq();
        return;
    }
    if ((m_registers[IC_CON / 4] & (1U << 4)) || (m_registers[IC_TAR / 4] & (1U << 12))) {
        abort_transfer(ABRT_7B_ADDR_NOACK);
        update_irq();
        return;
    }

    uint8_t data = static_cast<uint8_t>(command);
    tlm::tlm_generic_payload trans;
    dw_i2c_extension extension;
    extension.restart = command & DATA_CMD_RESTART;
    extension.stop = command & DATA_CMD_STOP;
    trans.set_extension(&extension);
    trans.set_address(m_registers[IC_TAR / 4] & 0x7f);
    trans.set_data_ptr(&data);
    trans.set_data_length(1);
    trans.set_streaming_width(1);
    trans.set_byte_enable_ptr(nullptr);
    trans.set_command(command & DATA_CMD_READ ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    i2c_socket->b_transport(trans, delay);
    trans.clear_extension(&extension);
    if (delay != sc_core::SC_ZERO_TIME) sc_core::wait(delay, m_reset_event);
    if (reset.read() || generation != m_reset_generation) return;

    if (trans.get_response_status() != tlm::TLM_OK_RESPONSE) {
        abort_transfer(trans.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE ? ABRT_7B_ADDR_NOACK
                                                                                      : ABRT_TXDATA_NOACK);
        update_irq();
        return;
    }

    if (command & DATA_CMD_READ) {
        if (m_rx_fifo.size() == FIFO_DEPTH) {
            m_latched_interrupts |= INTR_RX_OVER;
        } else {
            m_rx_fifo.push_back(data);
        }
    }
    if (command & DATA_CMD_STOP) {
        m_latched_interrupts |= INTR_STOP_DET;
        m_active = false;
    }
    update_irq();
}

dw_i2c_eeprom::dw_i2c_eeprom(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , p_address("address", 0x50, "7-bit I2C address")
    , p_size("size", 256, "EEPROM size in bytes")
    , p_address_width("address_width", 8, "EEPROM address width in bits")
    , p_page_size("page_size", 8, "EEPROM write page size")
    , p_access_latency("access_latency", sc_core::sc_time(100, sc_core::SC_NS), "EEPROM transaction latency")
    , i2c_socket("i2c_socket")
    , reset("reset")
    , m_storage(p_size.get_value(), 0xff)
{
    i2c_socket.register_b_transport(this, &dw_i2c_eeprom::b_transport);
    reset.register_value_changed_cb([this](bool asserted) {
        if (asserted) reset_transaction();
    });
}

void dw_i2c_eeprom::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    trans.set_dmi_allowed(false);
    if (!trans.get_data_ptr()) {
        trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
    }
    if (trans.get_byte_enable_ptr()) {
        trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        return;
    }
    if (trans.get_data_length() != 1 || trans.get_streaming_width() != 1) {
        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }
    if (trans.get_address() != (p_address.get_value() & 0x7f)) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }
    if (m_storage.empty() || p_address_width.get_value() != 8) {
        trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
    }

    auto* extension = trans.get_extension<dw_i2c_extension>();
    const bool restart = extension && extension->restart;
    const bool stop = extension && extension->stop;
    if (!m_transaction_active || restart) {
        m_transaction_active = true;
        if (trans.get_command() == tlm::TLM_WRITE_COMMAND) m_expect_address = true;
    }

    switch (trans.get_command()) {
    case tlm::TLM_WRITE_COMMAND:
        if (m_expect_address) {
            m_pointer = *trans.get_data_ptr() % m_storage.size();
            const uint32_t page_size = std::max<uint32_t>(1, p_page_size.get_value());
            m_page_base = (m_pointer / page_size) * page_size;
            m_expect_address = false;
        } else {
            m_storage[m_pointer] = *trans.get_data_ptr();
            advance_write_pointer();
        }
        break;
    case tlm::TLM_READ_COMMAND:
        *trans.get_data_ptr() = m_storage[m_pointer];
        m_pointer = (m_pointer + 1) % m_storage.size();
        break;
    default:
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    if (stop) reset_transaction();
    delay += p_access_latency.get_value();
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

void dw_i2c_eeprom::reset_transaction()
{
    m_transaction_active = false;
    m_expect_address = true;
}

void dw_i2c_eeprom::advance_write_pointer()
{
    const uint32_t page_size = std::max<uint32_t>(1, p_page_size.get_value());
    const uint32_t page_offset = (m_pointer + 1 - m_page_base) % page_size;
    m_pointer = (m_page_base + page_offset) % m_storage.size();
}

void module_register()
{
    GSC_MODULE_REGISTER_C(dw_apb_i2c);
    GSC_MODULE_REGISTER_C(dw_i2c_eeprom);
}
