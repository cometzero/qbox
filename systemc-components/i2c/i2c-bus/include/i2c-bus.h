/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef QBOX_I2C_BUS_H
#define QBOX_I2C_BUS_H

#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <module_factory_registery.h>
#include <tlm_sockets_buswidth.h>

class i2c_bus : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<i2c_bus, DEFAULT_TLM_BUSWIDTH> target_socket;
    tlm_utils::multi_passthrough_initiator_socket<i2c_bus, DEFAULT_TLM_BUSWIDTH> initiator_socket;

    explicit i2c_bus(sc_core::sc_module_name name);

private:
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
};

extern "C" void module_register();

#endif
