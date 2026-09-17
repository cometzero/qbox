/* SPDX-License-Identifier: BSD-3-Clause */
#include "i2c-bus.h"
#include <sstream>

i2c_bus::i2c_bus(sc_core::sc_module_name name)
    : sc_core::sc_module(name), p_trace("trace", false, "Trace I2C byte and lifecycle events"), target_socket("target_socket"), initiator_socket("initiator_socket")
{
    m_routes.fill(-1);
    target_socket.register_b_transport(this, &i2c_bus::b_transport);
}

void i2c_bus::start_of_simulation()
{
    for (unsigned address = 0; address < m_routes.size(); ++address) {
        for (unsigned i = 0; i < initiator_socket.size(); ++i) {
            tlm::tlm_generic_payload probe;
            dw_i2c_extension ext;
            ext.phase = dw_i2c_extension::event::discover;
            probe.set_extension(&ext);
            probe.set_address(address);
            probe.set_command(tlm::TLM_IGNORE_COMMAND);
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            initiator_socket[i]->b_transport(probe, delay);
            probe.clear_extension(&ext);
            if (probe.get_response_status() == tlm::TLM_OK_RESPONSE) {
                if (m_routes[address] >= 0) SC_REPORT_FATAL(name(), "Duplicate I2C target address/alias");
                m_routes[address] = i;
            }
        }
    }
}

void i2c_bus::trace(const char* phase, uint64_t address, int value)
{
    if (!p_trace.get_value()) return;
    std::ostringstream message;
    message << phase << " address=0x" << std::hex << address;
    if (value >= 0) message << " value=0x" << value;
    SC_REPORT_INFO(name(), message.str().c_str());
}

void i2c_bus::broadcast(dw_i2c_extension::event phase)
{
    using event_type = dw_i2c_extension::event;
    trace(phase == event_type::start ? "START" : phase == event_type::restart ? "RESTART" :
          phase == event_type::stop ? "STOP" : "CANCEL", m_address);
    for (unsigned i = 0; i < initiator_socket.size(); ++i) {
        tlm::tlm_generic_payload event;
        dw_i2c_extension ext;
        ext.phase = phase;
        event.set_extension(&ext);
        event.set_command(tlm::TLM_IGNORE_COMMAND);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        initiator_socket[i]->b_transport(event, delay);
        event.clear_extension(&ext);
    }
}

void i2c_bus::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    using event = dw_i2c_extension::event;
    auto* ext = trans.get_extension<dw_i2c_extension>();
    trans.set_dmi_allowed(false);
    if (ext && ext->phase != event::data) {
        if (ext->phase == event::cancel || ext->phase == event::stop) {
            broadcast(ext->phase);
            m_active = false;
            m_selected = -1;
        } else if (ext->phase == event::read_ack && m_selected >= 0) {
            trace(ext->ack ? "READ_ACK" : "READ_NACK", m_address);
            initiator_socket[m_selected]->b_transport(trans, delay);
        }
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return;
    }
    const auto address = trans.get_address();
    const bool boundary = !m_active || (ext && ext->restart) ||
                          address != m_address || trans.get_command() != m_direction;
    if (boundary) {
        m_address = address;
        broadcast(m_active ? event::restart : event::start);
        m_active = true;
        m_direction = trans.get_command();
        m_selected = address < m_routes.size() ? m_routes[address] : -1;
        if (m_selected >= 0) {
            tlm::tlm_generic_payload phase;
            dw_i2c_extension address_ext;
            address_ext.phase = event::address;
            phase.set_extension(&address_ext);
            phase.set_address(address);
            phase.set_command(trans.get_command());
            initiator_socket[m_selected]->b_transport(phase, delay);
            phase.clear_extension(&address_ext);
            if (phase.get_response_status() != tlm::TLM_OK_RESPONSE) m_selected = -1;
        }
        trace(m_selected >= 0 ? "ADDRESS_ACK" : "ADDRESS_NACK", address);
    }
    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    if (m_selected >= 0) {
        // STOP is a bus-wide event, sent after the byte and controller read NACK.
        const bool stop = ext && ext->stop;
        if (ext) ext->stop = false;
        initiator_socket[m_selected]->b_transport(trans, delay);
        if (ext) ext->stop = stop;
        if (trans.get_data_ptr()) trace(trans.get_command() == tlm::TLM_READ_COMMAND ? "READ" : "WRITE",
                                       address, *trans.get_data_ptr());
    }
    const auto status = trans.get_response_status();
    if (m_selected >= 0 && trans.get_command() == tlm::TLM_WRITE_COMMAND)
        trace(status == tlm::TLM_OK_RESPONSE ? "WRITE_ACK" : "WRITE_NACK", address);
    if (ext && ext->stop) {
        if (m_selected >= 0 && trans.get_command() == tlm::TLM_READ_COMMAND && status == tlm::TLM_OK_RESPONSE) {
            tlm::tlm_generic_payload response;
            dw_i2c_extension ack;
            ack.phase = event::read_ack;
            ack.ack = false;
            trace("READ_NACK", address);
            response.set_extension(&ack);
            response.set_command(tlm::TLM_IGNORE_COMMAND);
            sc_core::sc_time response_delay = sc_core::SC_ZERO_TIME;
            initiator_socket[m_selected]->b_transport(response, response_delay);
            response.clear_extension(&ack);
        }
        broadcast(event::stop);
        m_active = false;
        m_selected = -1;
    }
    trans.set_response_status(status);
}

void module_register() { GSC_MODULE_REGISTER_C(i2c_bus); }
