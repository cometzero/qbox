/* SPDX-License-Identifier: BSD-3-Clause */

#include "i2c-bus.h"

i2c_bus::i2c_bus(sc_core::sc_module_name name)
    : sc_core::sc_module(name), target_socket("target_socket"), initiator_socket("initiator_socket")
{
    target_socket.register_b_transport(this, &i2c_bus::b_transport);
}

void i2c_bus::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);

    // ponytail: first responder wins; use explicit routing if address collisions must be diagnosed.
    for (unsigned int i = 0; i < initiator_socket.size(); ++i) {
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        initiator_socket[i]->b_transport(trans, delay);
        if (trans.get_response_status() != tlm::TLM_ADDRESS_ERROR_RESPONSE) return;
    }

    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

void module_register() { GSC_MODULE_REGISTER_C(i2c_bus); }
