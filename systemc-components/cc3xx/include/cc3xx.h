/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <ports/target-signal-socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <cc3xx_core.h>

class cc3xx : public sc_core::sc_module
{
    using initiator_socket_type = tlm_utils::simple_initiator_socket_b<
        cc3xx, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types, sc_core::SC_ZERO_OR_MORE_BOUND>;

    using core_type = qbox::cc3xx::core;

    struct systemc_memory_if : public core_type::memory_if {
        explicit systemc_memory_if(cc3xx& owner): m_owner(owner) {}

        bool read(uint64_t address, uint8_t* data, unsigned int len) override
        {
            return m_owner.memory_access(tlm::TLM_READ_COMMAND, address, data, len);
        }

        bool write(uint64_t address, const uint8_t* data, unsigned int len) override
        {
            return m_owner.memory_access(tlm::TLM_WRITE_COMMAND, address,
                                         const_cast<uint8_t*>(data), len);
        }

        cc3xx& m_owner;
    };

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<unsigned int> p_trace_skip;
    cci::cci_param<std::string> p_trace_filter;
    cci::cci_param<uint64_t> p_trace_address_min;
    cci::cci_param<std::string> p_stats_file;
    cci::cci_param<unsigned int> p_stats_interval;
    initiator_socket_type initiator_socket;
    tlm_utils::simple_target_socket<cc3xx, DEFAULT_TLM_BUSWIDTH> target_socket;
    TargetSignalSocket<bool> reset;

private:
    core_type m_core;
    systemc_memory_if m_memory;
    sc_core::sc_time* m_current_delay = nullptr;
    bool m_stats_error_reported = false;

    static bool env_flag_enabled(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return false;
        }
        const std::string text(value);
        return text != "0" && text != "false" && text != "FALSE";
    }

    bool memory_access(tlm::tlm_command command, uint64_t address,
                       uint8_t* data, unsigned int len)
    {
        if (initiator_socket.size() == 0) {
            return false;
        }

        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        sc_core::sc_time& access_delay =
            m_current_delay == nullptr ? delay : *m_current_delay;
        tlm::tlm_generic_payload trans;

        trans.set_command(command);
        trans.set_address(address);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);

        initiator_socket->b_transport(trans, access_delay);
        return trans.is_response_ok();
    }

    void sync_core_config()
    {
        core_type::trace_config trace;

        trace.enabled = p_trace.get_value();
        trace.limit = p_trace_limit.get_value();
        trace.skip = p_trace_skip.get_value();
        trace.filter = p_trace_filter.get_value();
        trace.address_min = p_trace_address_min.get_value();
        m_core.set_trace_config(trace);
        m_core.set_stats_interval(p_stats_file.get_value().empty() ? 0 :
                                  p_stats_interval.get_value());
        m_core.set_timing_stats(env_flag_enabled("QBOX_CC3XX_TIMING_STATS"));
    }

    static void map_status(tlm::tlm_generic_payload& trans,
                           core_type::access_status status)
    {
        switch (status) {
        case core_type::access_status::ok:
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
            break;
        case core_type::access_status::address_error:
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            break;
        case core_type::access_status::command_error:
        default:
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            break;
        }
    }

    bool access(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();
        sc_core::sc_time* previous_delay = m_current_delay;
        core_type::access_result result;

        m_current_delay = &delay;
        sync_core_config();
        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            result = m_core.read(offset, data, len, debug);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            result = m_core.write(offset, data, len, debug);
        } else {
            result.status = core_type::access_status::command_error;
        }
        m_current_delay = previous_delay;

        map_status(trans, result.status);
        return result.status == core_type::access_status::ok;
    }

    void report_stats_error_once(const char* action, const std::string& path)
    {
        if (m_stats_error_reported) {
            return;
        }

        std::cerr << name() << " unable to " << action
                  << " stats_file=" << path << std::endl;
        m_stats_error_reported = true;
    }

    void write_stats_file()
    {
        const std::string path = p_stats_file.get_value();
        if (path.empty()) {
            return;
        }

        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out) {
            report_stats_error_once("open", path);
            return;
        }

        m_core.write_stats_json(out, name());
    }

public:
    explicit cc3xx(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_trace_skip("trace_skip", 0)
        , p_trace_filter("trace_filter", "all")
        , p_trace_address_min("trace_address_min", 0)
        , p_stats_file("stats_file", "")
        , p_stats_interval("stats_interval", 0)
        , initiator_socket("initiator_socket")
        , target_socket("target_socket")
        , reset("reset")
        , m_core(this->name())
        , m_memory(*this)
    {
        m_core.set_memory(&m_memory);
        m_core.set_stats_flush_callback([this] { write_stats_file(); });
        target_socket.register_b_transport(this, &cc3xx::b_transport);
        target_socket.register_transport_dbg(this, &cc3xx::transport_dbg);
        reset.register_value_changed_cb([this](bool value) { doreset(value); });
    }

    ~cc3xx() override
    {
        write_stats_file();
    }

    void doreset(bool value)
    {
        if (value) {
            m_core.reset(true);
            write_stats_file();
        }
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        trans.set_dmi_allowed(false);
        access(trans, delay, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        return access(trans, delay, true) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
