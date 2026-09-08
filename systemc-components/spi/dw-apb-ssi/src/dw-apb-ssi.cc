/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <cstring>

#include <dw-apb-ssi.h>

dw_apb_ssi::dw_apb_ssi(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , target_socket("target_socket")
    , irq("irq")
    , reset("reset")
    , pinmux_enable("pinmux_enable")
    , p_clock_frequency_hz("clock_frequency_hz", 100000000, "SSI input clock frequency in Hz")
    , p_fifo_depth("fifo_depth", DEFAULT_FIFO_DEPTH, "TX and RX FIFO depth in words")
    , p_num_chip_selects("num_chip_selects", 1, "Number of native chip selects")
    , p_access_latency_ns("access_latency_ns", 10, "MMIO access latency in nanoseconds")
    , m_irq_state(false)
{
    reset_state();
    target_socket.register_b_transport(this, &dw_apb_ssi::b_transport);
    reset.register_value_changed_cb([this](bool asserted) {
        if (asserted) {
            reset_state();
            m_cancel_event.notify(sc_core::SC_ZERO_TIME);
        }
    });
    pinmux_enable.register_value_changed_cb([this](bool enabled) { m_pinmux_enabled = enabled; });

    SC_METHOD(drive_irq);
    sensitive << m_irq_event;
    SC_THREAD(transfer_thread);
}

void dw_apb_ssi::reset_state()
{
    const bool irq_was_asserted = m_irq_state;
    m_ctrlr0 = 0x7; // Eight-bit Motorola SPI frames.
    m_ctrlr1 = 0;
    m_ssienr = 0;
    m_mwcr = 0;
    m_ser = 0;
    m_baudr = 2;
    m_txftlr = 0;
    m_rxftlr = 0;
    m_imr = 0;
    m_sticky_interrupts = 0;
    m_dmatdlr = 0;
    m_dmardlr = 0;
    m_rx_sample_dly = 0;
    m_busy = false;
    m_irq_state = false;
    m_tx_fifo.clear();
    m_rx_fifo.clear();
    if (irq_was_asserted) m_irq_event.notify(sc_core::SC_ZERO_TIME);
}

uint32_t dw_apb_ssi::status() const
{
    uint32_t value = 0;
    if (m_busy) value |= SR_BUSY;
    if (m_tx_fifo.size() < fifo_depth()) value |= SR_TF_NOT_FULL;
    if (m_tx_fifo.empty()) value |= SR_TF_EMPTY;
    if (!m_rx_fifo.empty()) value |= SR_RF_NOT_EMPTY;
    if (m_rx_fifo.size() == fifo_depth()) value |= SR_RF_FULL;
    return value;
}

uint32_t dw_apb_ssi::raw_interrupt_status() const
{
    uint32_t value = m_sticky_interrupts;
    if (m_tx_fifo.size() <= m_txftlr) value |= INT_TXEI;
    if (m_rx_fifo.size() > m_rxftlr) value |= INT_RXFI;
    return value & INT_MASK;
}

void dw_apb_ssi::update_irq()
{
    const bool state = (raw_interrupt_status() & m_imr) != 0;
    if (state != m_irq_state) {
        m_irq_state = state;
        m_irq_event.notify(sc_core::SC_ZERO_TIME);
    }
}

void dw_apb_ssi::drive_irq() { irq->write(m_irq_state); }

uint32_t dw_apb_ssi::data_mask() const
{
    const unsigned int bits = std::max(4u, std::min(16u, (m_ctrlr0 & 0xf) + 1));
    return (1u << bits) - 1;
}

uint32_t dw_apb_ssi::fifo_depth() const { return std::max(2u, std::min(256u, p_fifo_depth.get_value())); }

uint32_t dw_apb_ssi::chip_select_mask() const
{
    const uint32_t count = std::max(1u, std::min(16u, p_num_chip_selects.get_value()));
    return (1u << count) - 1;
}

uint32_t dw_apb_ssi::writable_value(uint32_t offset) const
{
    switch (offset) {
    case CTRLR0:
        return m_ctrlr0;
    case CTRLR1:
        return m_ctrlr1;
    case SSIENR:
        return m_ssienr;
    case MWCR:
        return m_mwcr;
    case SER:
        return m_ser;
    case BAUDR:
        return m_baudr;
    case TXFTLR:
        return m_txftlr;
    case RXFTLR:
        return m_rxftlr;
    case IMR:
        return m_imr;
    case DMATDLR:
        return m_dmatdlr;
    case DMARDLR:
        return m_dmardlr;
    case RX_SAMPLE_DLY:
        return m_rx_sample_dly;
    default:
        return 0;
    }
}

sc_core::sc_time dw_apb_ssi::frame_delay() const
{
    const uint64_t frequency = std::max<uint64_t>(1, p_clock_frequency_hz.get_value());
    const unsigned int bits = std::max(4u, std::min(16u, (m_ctrlr0 & 0xf) + 1));
    return sc_core::sc_time(static_cast<double>(bits) * m_baudr / frequency, sc_core::SC_SEC);
}

uint32_t dw_apb_ssi::read_register(uint32_t offset)
{
    switch (offset) {
    case CTRLR0:
        return m_ctrlr0;
    case CTRLR1:
        return m_ctrlr1;
    case SSIENR:
        return m_ssienr;
    case MWCR:
        return m_mwcr;
    case SER:
        return m_ser;
    case BAUDR:
        return m_baudr;
    case TXFTLR:
        return m_txftlr;
    case RXFTLR:
        return m_rxftlr;
    case TXFLR:
        return static_cast<uint32_t>(m_tx_fifo.size());
    case RXFLR:
        return static_cast<uint32_t>(m_rx_fifo.size());
    case SR:
        return status();
    case IMR:
        return m_imr;
    case ISR:
        return raw_interrupt_status() & m_imr;
    case RISR:
        return raw_interrupt_status();
    case TXOICR: {
        const uint32_t value = m_sticky_interrupts & INT_TXOI;
        m_sticky_interrupts &= ~INT_TXOI;
        update_irq();
        return value;
    }
    case RXOICR: {
        const uint32_t value = m_sticky_interrupts & INT_RXOI;
        m_sticky_interrupts &= ~INT_RXOI;
        update_irq();
        return value;
    }
    case RXUICR: {
        const uint32_t value = m_sticky_interrupts & INT_RXUI;
        m_sticky_interrupts &= ~INT_RXUI;
        update_irq();
        return value;
    }
    case MSTICR: {
        const uint32_t value = m_sticky_interrupts & INT_MSTI;
        m_sticky_interrupts &= ~INT_MSTI;
        update_irq();
        return value;
    }
    case ICR: {
        const uint32_t value = m_sticky_interrupts;
        m_sticky_interrupts = 0;
        update_irq();
        return value;
    }
    case DMACR:
        return 0; // DMA is intentionally not modelled.
    case DMATDLR:
        return m_dmatdlr;
    case DMARDLR:
        return m_dmardlr;
    case IDR:
        return COMPONENT_ID;
    case VERSION:
        return COMPONENT_VERSION;
    case DR: {
        if (m_rx_fifo.empty()) {
            m_sticky_interrupts |= INT_RXUI;
            update_irq();
            return 0;
        }
        const uint32_t value = m_rx_fifo.front();
        m_rx_fifo.pop_front();
        update_irq();
        return value;
    }
    case RX_SAMPLE_DLY:
        return m_rx_sample_dly;
    default:
        return 0;
    }
}

void dw_apb_ssi::write_register(uint32_t offset, uint32_t value)
{
    switch (offset) {
    case CTRLR0:
        if (!m_ssienr) m_ctrlr0 = value & CTRLR0_MASK;
        break;
    case CTRLR1:
        if (!m_ssienr) m_ctrlr1 = value & 0xffff;
        break;
    case SSIENR: {
        const bool was_enabled = m_ssienr != 0;
        m_ssienr = value & 1;
        if (!m_ssienr) {
            m_busy = false;
            m_tx_fifo.clear();
            m_rx_fifo.clear();
            if (was_enabled) m_cancel_event.notify(sc_core::SC_ZERO_TIME);
        } else if (!was_enabled && m_ssienr && !m_tx_fifo.empty()) {
            m_transfer_event.notify(sc_core::SC_ZERO_TIME);
        }
        break;
    }
    case MWCR:
        if (!m_ssienr) m_mwcr = value;
        break;
    case SER:
        m_ser = value & chip_select_mask();
        break;
    case BAUDR:
        if (!m_ssienr) {
            m_baudr = std::max(2u, value & 0xfffeu);
        }
        break;
    case TXFTLR:
        m_txftlr = std::min(value, fifo_depth() - 1);
        break;
    case RXFTLR:
        m_rxftlr = std::min(value, fifo_depth() - 1);
        break;
    case IMR:
        m_imr = value & INT_MASK;
        break;
    case DMACR:
        break; // No DMA request interface is exposed by this model.
    case DMATDLR:
        m_dmatdlr = value;
        break;
    case DMARDLR:
        m_dmardlr = value;
        break;
    case DR:
        if (m_tx_fifo.size() == fifo_depth()) {
            m_sticky_interrupts |= INT_TXOI;
        } else {
            m_tx_fifo.push_back(value & data_mask());
            if (m_ssienr) m_transfer_event.notify(sc_core::SC_ZERO_TIME);
        }
        break;
    case RX_SAMPLE_DLY:
        m_rx_sample_dly = value;
        break;
    default:
        break;
    }
    update_irq();
}

void dw_apb_ssi::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    const uint64_t address = trans.get_address();
    const unsigned int length = trans.get_data_length();
    if (address >= 0x100 || !trans.get_data_ptr() || (length != 1 && length != 2 && length != 4) ||
        (address % length) != 0 || ((address & 3) + length) > 4) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }
    if (trans.get_byte_enable_ptr()) {
        trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        return;
    }
    if (trans.get_streaming_width() != length) {
        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }

    uint32_t value = 0;
    const uint32_t offset = static_cast<uint32_t>(address & ~uint64_t(3));
    switch (trans.get_command()) {
    case tlm::TLM_READ_COMMAND:
        value = read_register(offset);
        std::memcpy(trans.get_data_ptr(), reinterpret_cast<uint8_t*>(&value) + (address & 3), length);
        break;
    case tlm::TLM_WRITE_COMMAND:
        value = writable_value(offset);
        std::memcpy(reinterpret_cast<uint8_t*>(&value) + (address & 3), trans.get_data_ptr(), length);
        write_register(offset, value);
        break;
    default:
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    delay += sc_core::sc_time(p_access_latency_ns.get_value(), sc_core::SC_NS);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

void dw_apb_ssi::transfer_thread()
{
    while (true) {
        wait(m_transfer_event);
        while (m_ssienr && !m_tx_fifo.empty()) {
            m_busy = true;
            update_irq();
            wait(frame_delay(), m_cancel_event);
            if (!m_ssienr) break;

            const uint32_t value = m_tx_fifo.front();
            m_tx_fifo.pop_front();
            if (m_pinmux_enabled && (m_ctrlr0 & (1u << 11))) {
                if (m_rx_fifo.size() == fifo_depth()) {
                    m_sticky_interrupts |= INT_RXOI;
                } else {
                    m_rx_fifo.push_back(value);
                }
            }
            update_irq();
        }
        m_busy = false;
        update_irq();
    }
}

void module_register() { GSC_MODULE_REGISTER_C(dw_apb_ssi); }
