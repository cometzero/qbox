/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <cstring>

#include <dw-apb-uart.h>

dw_apb_uart::dw_apb_uart(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , p_clock_frequency_hz("clock_frequency_hz", 100000000ull)
    , p_access_latency_ns("access_latency_ns", 10ull)
    , target_socket("target_socket")
    , irq("irq")
    , backend_socket("backend_socket")
    , reset("reset")
    , m_irq_stub("irq_stub")
{
    target_socket.register_b_transport(this, &dw_apb_uart::b_transport);
    backend_socket.register_b_transport(this, &dw_apb_uart::receive);
    reset.register_value_changed_cb([this](bool asserted) { reset_changed(asserted); });

    SC_THREAD(tx_thread);
    SC_THREAD(rx_timeout_thread);
    SC_METHOD(update_irq);
    sensitive << m_irq_event;

    reset_registers();
}

void dw_apb_uart::before_end_of_elaboration()
{
    if (!irq.get_interface()) {
        irq.bind(m_irq_stub);
    }
}

void dw_apb_uart::end_of_elaboration() { refresh_rx_credit(); }

unsigned int dw_apb_uart::fifo_capacity() const { return (m_fcr & FCR_ENABLE_FIFO) ? FIFO_DEPTH : 1; }

unsigned int dw_apb_uart::rx_trigger() const
{
    if (!(m_fcr & FCR_ENABLE_FIFO)) {
        return 1;
    }

    static constexpr unsigned int levels[] = { 1, 4, 8, 14 };
    return levels[(m_fcr >> 6) & 0x3];
}

bool dw_apb_uart::busy() const { return !m_tx_fifo.empty(); }

sc_core::sc_time dw_apb_uart::character_time() const
{
    const uint64_t frequency = std::max<uint64_t>(1, p_clock_frequency_hz.get_value());
    const uint64_t divisor = std::max<uint64_t>(1, (uint16_t(m_dlh) << 8) | m_dll);
    const unsigned int data_bits = 5 + (m_lcr & 0x3);
    const unsigned int parity_bits = (m_lcr & 0x08) ? 1 : 0;
    const unsigned int stop_bits = (m_lcr & 0x04) ? 2 : 1;
    const unsigned int frame_bits = 1 + data_bits + parity_bits + stop_bits;
    const double seconds = (16.0 * divisor * frame_bits) / frequency;
    return sc_core::sc_time(seconds, sc_core::SC_SEC);
}

void dw_apb_uart::schedule_irq() { m_irq_event.notify(sc_core::SC_ZERO_TIME); }

uint8_t dw_apb_uart::interrupt_id(bool acknowledge_thre)
{
    uint8_t value = (m_fcr & FCR_ENABLE_FIFO) ? IIR_FIFO_ENABLED : 0;

    if ((m_ier & IER_RLSI) && m_lsr_errors) {
        return value | IIR_RLSI;
    }
    if ((m_ier & IER_RDI) && m_rx_fifo.size() >= rx_trigger()) {
        return value | IIR_RDI;
    }
    if ((m_ier & IER_RDI) && m_rx_timeout_pending) {
        return value | IIR_RX_TIMEOUT;
    }
    if (m_busy_pending) {
        return value | IIR_BUSY;
    }
    if ((m_ier & IER_THRI) && m_thre_pending) {
        if (acknowledge_thre) {
            m_thre_pending = false;
            schedule_irq();
        }
        return value | IIR_THRI;
    }
    if ((m_ier & IER_MSI) && (m_msr & 0x0f)) {
        return value | IIR_MSI;
    }
    return value | IIR_NO_INT;
}

void dw_apb_uart::update_irq() { irq->write((interrupt_id(false) & 0x0f) != IIR_NO_INT); }

void dw_apb_uart::refresh_rx_credit()
{
    const unsigned int capacity = fifo_capacity();
    const unsigned int available = !m_reset_asserted && capacity > m_rx_fifo.size() ? capacity - m_rx_fifo.size() : 0;
    backend_socket.can_receive_set(available);
}

void dw_apb_uart::reset_registers()
{
    m_rx_fifo.clear();
    m_tx_fifo.clear();
    m_dll = 0;
    m_dlh = 0;
    m_ier = 0;
    m_fcr = 0;
    m_lcr = 0;
    m_mcr = 0;
    m_msr = 0xb0;
    m_scr = 0;
    m_lsr_errors = 0;
    m_thre_pending = true;
    m_rx_timeout_pending = false;
    m_busy_pending = false;
    ++m_state_epoch;
    ++m_rx_epoch;
    m_state_event.notify(sc_core::SC_ZERO_TIME);
    m_rx_event.notify(sc_core::SC_ZERO_TIME);
    schedule_irq();
}

void dw_apb_uart::reset_changed(bool asserted)
{
    m_reset_asserted = asserted;
    if (asserted) {
        backend_socket.reset();
        reset_registers();
        refresh_rx_credit();
    } else {
        ++m_state_epoch;
        m_state_event.notify(sc_core::SC_ZERO_TIME);
        refresh_rx_credit();
    }
}

void dw_apb_uart::receive_byte(uint8_t data)
{
    if (m_reset_asserted) {
        return;
    }

    if (m_rx_fifo.size() >= fifo_capacity()) {
        m_lsr_errors |= LSR_OE;
    } else {
        m_rx_fifo.push_back(data);
    }
    m_rx_timeout_pending = false;
    ++m_rx_epoch;
    m_rx_event.notify(sc_core::SC_ZERO_TIME);
    schedule_irq();
}

void dw_apb_uart::receive(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    (void)delay;
    const unsigned int length = trans.get_data_length();
    const unsigned int width = trans.get_streaming_width() ? trans.get_streaming_width() : length;
    uint8_t* data = trans.get_data_ptr();

    if (!data || width < length) {
        trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
    }

    for (unsigned int i = 0; i < length; ++i) {
        receive_byte(data[i]);
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

void dw_apb_uart::tx_thread()
{
    while (true) {
        while (m_reset_asserted || m_tx_fifo.empty()) {
            wait(m_tx_event | m_state_event);
        }

        const uint64_t epoch = m_state_epoch;
        wait(character_time(), m_state_event);
        if (m_reset_asserted || epoch != m_state_epoch || m_tx_fifo.empty()) {
            continue;
        }

        const uint8_t data = m_tx_fifo.front();
        m_tx_fifo.pop_front();
        if (m_mcr & MCR_LOOP) {
            receive_byte(data);
        } else {
            backend_socket.enqueue(data);
        }
        if (m_tx_fifo.empty()) {
            m_thre_pending = true;
        }
        schedule_irq();
    }
}

void dw_apb_uart::rx_timeout_thread()
{
    while (true) {
        while (m_reset_asserted || !(m_fcr & FCR_ENABLE_FIFO) || m_rx_fifo.empty() ||
               m_rx_fifo.size() >= rx_trigger()) {
            wait(m_rx_event | m_state_event);
        }

        const uint64_t epoch = m_rx_epoch;
        wait(character_time() * 4.0, m_rx_event | m_state_event);
        if (!m_reset_asserted && epoch == m_rx_epoch && (m_fcr & FCR_ENABLE_FIFO) && !m_rx_fifo.empty() &&
            m_rx_fifo.size() < rx_trigger()) {
            m_rx_timeout_pending = true;
            schedule_irq();
            wait(m_rx_event | m_state_event);
        }
    }
}

uint32_t dw_apb_uart::read_register(uint32_t offset)
{
    switch (offset) {
    case RBR_THR_DLL:
        if (m_lcr & LCR_DLAB) {
            return m_dll;
        }
        if (!m_rx_fifo.empty()) {
            const uint8_t data = m_rx_fifo.front();
            m_rx_fifo.pop_front();
            m_rx_timeout_pending = false;
            ++m_rx_epoch;
            m_rx_event.notify(sc_core::SC_ZERO_TIME);
            refresh_rx_credit();
            schedule_irq();
            return data;
        }
        return 0;
    case IER_DLH:
        return (m_lcr & LCR_DLAB) ? m_dlh : m_ier;
    case IIR_FCR:
        return interrupt_id(true);
    case LCR:
        return m_lcr;
    case MCR:
        return m_mcr;
    case LSR: {
        uint8_t value = m_lsr_errors;
        if (!m_rx_fifo.empty()) value |= LSR_DR;
        if (m_tx_fifo.empty()) value |= LSR_THRE | LSR_TEMT;
        m_lsr_errors = 0;
        schedule_irq();
        return value;
    }
    case MSR: {
        const uint8_t value = m_msr;
        m_msr &= 0xf0;
        schedule_irq();
        return value;
    }
    case SCR:
        return m_scr;
    case USR: {
        uint8_t value = USR_TFNF;
        if (busy()) value |= USR_BUSY;
        if (m_tx_fifo.empty()) value |= USR_TFE;
        if (!m_rx_fifo.empty()) value |= USR_RFNE;
        if (m_rx_fifo.size() >= fifo_capacity()) value |= USR_RFF;
        m_busy_pending = false;
        schedule_irq();
        return value;
    }
    case TFL:
        return m_tx_fifo.size();
    case RFL:
        return m_rx_fifo.size();
    case DLF:
        return 0;
    case CPR:
        return COMPONENT_PARAMETER;
    case UCV:
        return COMPONENT_VERSION;
    case CTR:
        return COMPONENT_TYPE;
    default:
        return 0;
    }
}

void dw_apb_uart::write_register(uint32_t offset, uint32_t value)
{
    const uint8_t byte = value & 0xff;

    switch (offset) {
    case RBR_THR_DLL:
        if (m_lcr & LCR_DLAB) {
            m_dll = byte;
            ++m_state_epoch;
            m_state_event.notify(sc_core::SC_ZERO_TIME);
        } else if (!m_reset_asserted && m_tx_fifo.size() < fifo_capacity()) {
            m_tx_fifo.push_back(byte);
            m_thre_pending = false;
            m_tx_event.notify(sc_core::SC_ZERO_TIME);
            schedule_irq();
        }
        break;
    case IER_DLH:
        if (m_lcr & LCR_DLAB) {
            m_dlh = byte;
            ++m_state_epoch;
            m_state_event.notify(sc_core::SC_ZERO_TIME);
        } else {
            const bool enable_thre = !(m_ier & IER_THRI) && (byte & IER_THRI) && m_tx_fifo.empty();
            m_ier = byte & 0x0f;
            if (enable_thre) m_thre_pending = true;
            schedule_irq();
        }
        break;
    case IIR_FCR: {
        const bool enable_changed = (m_fcr ^ byte) & FCR_ENABLE_FIFO;
        m_fcr = byte & (FCR_ENABLE_FIFO | 0xc8);
        if (enable_changed || (byte & FCR_CLEAR_RCVR)) {
            m_rx_fifo.clear();
            m_rx_timeout_pending = false;
            ++m_rx_epoch;
            m_rx_event.notify(sc_core::SC_ZERO_TIME);
            refresh_rx_credit();
        }
        if (enable_changed || (byte & FCR_CLEAR_XMIT)) {
            m_tx_fifo.clear();
            m_thre_pending = true;
            ++m_state_epoch;
            m_state_event.notify(sc_core::SC_ZERO_TIME);
        }
        schedule_irq();
        break;
    }
    case LCR:
        if (busy()) {
            m_busy_pending = true;
        } else {
            m_lcr = byte;
            ++m_state_epoch;
            m_state_event.notify(sc_core::SC_ZERO_TIME);
        }
        schedule_irq();
        break;
    case MCR:
        m_mcr = byte & 0x3f;
        break;
    case SCR:
        m_scr = byte;
        break;
    case SRR:
        if (byte & 0x01) {
            backend_socket.reset();
            reset_registers();
            refresh_rx_credit();
        } else {
            if (byte & 0x02) {
                m_rx_fifo.clear();
                m_rx_timeout_pending = false;
                ++m_rx_epoch;
                m_rx_event.notify(sc_core::SC_ZERO_TIME);
                refresh_rx_credit();
            }
            if (byte & 0x04) {
                m_tx_fifo.clear();
                m_thre_pending = true;
                ++m_state_epoch;
                m_state_event.notify(sc_core::SC_ZERO_TIME);
            }
            schedule_irq();
        }
        break;
    case DLF:
        break;
    default:
        break;
    }
}

void dw_apb_uart::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    trans.set_dmi_allowed(false);

    const uint64_t address = trans.get_address();
    const unsigned int length = trans.get_data_length();
    if (address > CTR || (address & 0x3)) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }
    if ((length != 1 && length != 4) || !trans.get_data_ptr()) {
        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }
    if (trans.get_streaming_width() < length) {
        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }
    if (trans.get_byte_enable_ptr()) {
        trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        return;
    }
    if (trans.get_command() != tlm::TLM_READ_COMMAND && trans.get_command() != tlm::TLM_WRITE_COMMAND) {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    uint32_t value = 0;
    if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
        std::memcpy(&value, trans.get_data_ptr(), length);
        write_register(address, value);
    } else {
        value = read_register(address);
        std::memcpy(trans.get_data_ptr(), &value, length);
    }

    delay += sc_core::sc_time(p_access_latency_ns.get_value(), sc_core::SC_NS);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

void module_register() { GSC_MODULE_REGISTER_C(dw_apb_uart); }
