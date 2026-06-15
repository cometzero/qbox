/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <iostream>
#include <utility>
#include <vector>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <ports/multiinitiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <systemc>

class reset_fanout : public sc_core::sc_module
{
    unsigned int m_trace_count = 0;
    std::vector<bool> m_pending_resets;
    bool m_last_reset = false;
    bool m_have_last_reset = false;
    sc_core::sc_event m_reset_event;

    void trace_write(bool value)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value())
            return;

        ++m_trace_count;
        std::cerr << name() << " reset=" << value
                  << " targets=" << reset_out.size()
                  << " sc_time=" << sc_core::sc_time_stamp()
                  << std::endl;
    }

public:
    SC_HAS_PROCESS(reset_fanout);

    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    TargetSignalSocket<bool> reset_in;
    MultiInitiatorSignalSocket<> reset_out;

    explicit reset_fanout(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , reset_in("reset_in")
        , reset_out("reset_out")
    {
        reset_in.register_value_changed_cb([this](bool value) {
            if (m_have_last_reset && value == m_last_reset)
                return;

            m_have_last_reset = true;
            m_last_reset = value;
            trace_write(value);
            m_pending_resets.push_back(value);
            m_reset_event.notify(sc_core::sc_time(1, sc_core::SC_PS));
        });

        SC_METHOD(emit_reset);
        sensitive << m_reset_event;
        dont_initialize();
    }

    void emit_reset()
    {
        const std::vector<bool> pending = std::move(m_pending_resets);
        m_pending_resets.clear();
        reset_out.async_write_vector(pending);
    }
};
