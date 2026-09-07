/* SPDX-License-Identifier: BSD-3-Clause */

#include "pca9539.h"

#include <i2c-transaction.h>

pca9539::pca9539(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , p_address("address", 0x74, "7-bit I2C address")
    , p_access_latency("access_latency", sc_core::sc_time(100, sc_core::SC_NS), "I2C transaction latency")
    , i2c_socket("i2c_socket")
    , reset_n("reset_n")
    , int_n("int_n")
    , gpio_in("gpio_in", NUM_GPIOS)
    , gpio_out("gpio_out", NUM_GPIOS)
    , gpio_oe("gpio_oe", NUM_GPIOS)
    , m_output_event(false)
    , m_irq_event(false)
    , m_irq_stub("irq_stub")
    , m_output_stubs("output_stub", NUM_GPIOS)
    , m_oe_stubs("oe_stub", NUM_GPIOS)
{
    i2c_socket.register_b_transport(this, &pca9539::b_transport);
    reset_n.register_value_changed_cb([this](bool level) { set_reset_n(level); });
    for (unsigned int pin = 0; pin < NUM_GPIOS; ++pin) {
        gpio_in[pin].register_value_changed_cb([this, pin](bool level) { set_gpio_input(pin, level); });
    }

    SC_METHOD(drive_interrupt);
    sensitive << m_irq_event;
    SC_METHOD(drive_outputs);
    sensitive << m_output_event;
}

void pca9539::before_end_of_elaboration()
{
    if (!int_n.get_interface()) int_n.bind(m_irq_stub);
    for (unsigned int pin = 0; pin < NUM_GPIOS; ++pin) {
        if (!gpio_out[pin].get_interface()) gpio_out[pin].bind(m_output_stubs[pin]);
        if (!gpio_oe[pin].get_interface()) gpio_oe[pin].bind(m_oe_stubs[pin]);
    }
}

void pca9539::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
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
    if (trans.get_address() != (p_address.get_value() & 0x7f) || m_reset_asserted) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    auto* extension = trans.get_extension<dw_i2c_extension>();
    const bool restart = extension && extension->restart;
    const bool stop = extension && extension->stop;
    if (!m_transaction_active || restart) {
        m_transaction_active = true;
        if (trans.get_command() == tlm::TLM_WRITE_COMMAND) m_expect_command = true;
    }

    switch (trans.get_command()) {
    case tlm::TLM_WRITE_COMMAND:
        if (m_expect_command) {
            m_pointer = *trans.get_data_ptr() & 0x07;
            m_expect_command = false;
        } else {
            write_register(m_pointer, *trans.get_data_ptr());
            advance_pointer();
        }
        break;
    case tlm::TLM_READ_COMMAND:
        *trans.get_data_ptr() = read_register(m_pointer);
        advance_pointer();
        break;
    default:
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    if (stop) {
        m_transaction_active = false;
        m_expect_command = true;
    }
    delay += p_access_latency.get_value();
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

void pca9539::reset_device()
{
    m_output = { { 0xff, 0xff } };
    m_polarity = { { 0x00, 0x00 } };
    m_config = { { 0xff, 0xff } };
    m_pointer = INPUT_PORT_0;
    m_transaction_active = false;
    m_expect_command = true;
    m_input_latch = { { physical_port(0), physical_port(1) } };
    m_int_n_level = true;
    m_output_event.notify(sc_core::SC_ZERO_TIME);
    m_irq_event.notify();
}

void pca9539::set_reset_n(bool level)
{
    if (!level) {
        m_reset_asserted = true;
        reset_device();
        return;
    }
    if (m_reset_asserted) {
        m_reset_asserted = false;
        m_input_latch = { { physical_port(0), physical_port(1) } };
        update_interrupt();
    }
}

void pca9539::set_gpio_input(unsigned int pin, bool level)
{
    if (m_input_levels[pin] == level) return;
    m_input_levels[pin] = level;
    m_output_event.notify(sc_core::SC_ZERO_TIME);
    if (m_reset_asserted) {
        m_input_latch = { { physical_port(0), physical_port(1) } };
        return;
    }
    update_interrupt();
}

uint8_t pca9539::physical_port(unsigned int bank) const
{
    uint8_t value = 0;
    for (unsigned int bit = 0; bit < 8; ++bit) {
        const unsigned int pin = bank * 8 + bit;
        const bool input = m_config[bank] & (1U << bit);
        const bool level = input ? m_input_levels[pin] : (m_output[bank] & (1U << bit));
        if (level) value |= 1U << bit;
    }
    return value;
}

uint8_t pca9539::read_register(uint8_t reg)
{
    const unsigned int bank = reg & 1U;
    switch (reg) {
    case INPUT_PORT_0:
    case INPUT_PORT_1: {
        const uint8_t physical = physical_port(bank);
        m_input_latch[bank] = physical;
        update_interrupt();
        return physical ^ (m_polarity[bank] & m_config[bank]);
    }
    case OUTPUT_PORT_0:
    case OUTPUT_PORT_1:
        return m_output[bank];
    case POLARITY_PORT_0:
    case POLARITY_PORT_1:
        return m_polarity[bank];
    case CONFIG_PORT_0:
    case CONFIG_PORT_1:
        return m_config[bank];
    default:
        return 0xff;
    }
}

void pca9539::write_register(uint8_t reg, uint8_t value)
{
    const unsigned int bank = reg & 1U;
    switch (reg) {
    case OUTPUT_PORT_0:
    case OUTPUT_PORT_1:
        m_output[bank] = value;
        m_output_event.notify();
        break;
    case POLARITY_PORT_0:
    case POLARITY_PORT_1:
        m_polarity[bank] = value;
        break;
    case CONFIG_PORT_0:
    case CONFIG_PORT_1:
        m_config[bank] = value;
        m_output_event.notify();
        update_interrupt();
        break;
    default:
        break;
    }
}

void pca9539::advance_pointer() { m_pointer ^= 1U; }

void pca9539::update_interrupt()
{
    bool pending = false;
    for (unsigned int bank = 0; bank < 2; ++bank) {
        pending |= ((physical_port(bank) ^ m_input_latch[bank]) & m_config[bank]) != 0;
    }
    const bool level = !pending;
    if (level != m_int_n_level) {
        m_int_n_level = level;
        m_irq_event.notify();
    }
}

void pca9539::drive_interrupt() { int_n->write(m_int_n_level); }

void pca9539::drive_outputs()
{
    for (unsigned int pin = 0; pin < NUM_GPIOS; ++pin) {
        const unsigned int bank = pin / 8;
        const unsigned int bit = pin % 8;
        gpio_out[pin]->write((physical_port(bank) & (1U << bit)) != 0);
        gpio_oe[pin]->write((m_config[bank] & (1U << bit)) == 0);
    }
}

void module_register() { GSC_MODULE_REGISTER_C(pca9539); }
