/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <systemc>

class ras_ffh_stub : public sc_core::sc_module
{
public:
    InitiatorSignalSocket<bool> irq;

    explicit ras_ffh_stub(sc_core::sc_module_name name)
        : sc_core::sc_module(name), irq("irq")
    {
    }
};

extern "C" void module_register();
