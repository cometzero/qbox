/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class host_gtimer : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x1000;
    static constexpr uint32_t PCTL = 0x000;
    static constexpr uint32_t PCTH = 0x004;
    static constexpr uint32_t FRQ = 0x010;

    std::array<uint8_t, REG_BYTES> m_regs{};
    uint64_t m_counter = 0;
    unsigned int m_trace_count = 0;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    uint32_t load32(uint32_t offset) const
    {
        uint32_t value = 0;
        std::memcpy(&value, &m_regs[offset], sizeof(value));
        return value;
    }

    void store32(uint32_t offset, uint32_t value)
    {
        std::memcpy(&m_regs[offset], &value, sizeof(value));
    }

    uint64_t next_counter_value()
    {
        m_counter += p_counter_increment.get_value();
        return m_counter;
    }

    uint32_t read32(uint32_t offset)
    {
        if (p_counter_base.get_value()) {
            if (offset == PCTL) {
                return static_cast<uint32_t>(next_counter_value());
            }
            if (offset == PCTH) {
                return static_cast<uint32_t>(m_counter >> 32);
            }
            if (offset == FRQ) {
                return p_frequency.get_value();
            }
        }

        return load32(offset);
    }

    void reset_registers()
    {
        m_regs.fill(0);
        m_counter = 0;
        store32(FRQ, p_frequency.get_value());
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) ||
            offset + len > m_regs.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            if (len == sizeof(uint32_t) && (offset % sizeof(uint32_t)) == 0) {
                const auto value = read32(static_cast<uint32_t>(offset));
                std::memcpy(data, &value, sizeof(value));
            } else {
                std::memcpy(data, &m_regs[offset], len);
            }
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            std::memcpy(&m_regs[offset], data, len);
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trace_access(trans, offset, len, debug);

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset,
                      unsigned int len, bool debug)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value())
            return;

        ++m_trace_count;
        uint32_t value = 0;
        if (len <= sizeof(value))
            std::memcpy(&value, trans.get_data_ptr(), len);

        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" :
                                                                      "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

public:
    cci::cci_param<bool> p_counter_base;
    cci::cci_param<uint32_t> p_frequency;
    cci::cci_param<uint64_t> p_counter_increment;
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    tlm_utils::simple_target_socket<host_gtimer, DEFAULT_TLM_BUSWIDTH>
        target_socket;

    explicit host_gtimer(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_counter_base("counter_base", false)
        , p_frequency("frequency", 125000000)
        , p_counter_increment("counter_increment", 4096)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &host_gtimer::b_transport);
        target_socket.register_transport_dbg(this, &host_gtimer::transport_dbg);
    }

    void before_end_of_elaboration() override
    {
        reset_registers();
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access(trans, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans, true) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
