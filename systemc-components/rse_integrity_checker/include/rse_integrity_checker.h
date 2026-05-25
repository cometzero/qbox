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

class rse_integrity_checker : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x1000;
    static constexpr uint32_t ICBC = 0x000;
    static constexpr uint32_t ICC = 0x004;
    static constexpr uint32_t ICIS = 0x008;
    static constexpr uint32_t ICIE = 0x00c;
    static constexpr uint32_t ICAE = 0x010;
    static constexpr uint32_t ICIC = 0x014;
    static constexpr uint32_t ICDA = 0x018;
    static constexpr uint32_t ICDL = 0x01c;
    static constexpr uint32_t ICEVA = 0x020;
    static constexpr uint32_t ICCVA = 0x024;
    static constexpr uint32_t ICCVAL_BASE = 0x028;
    static constexpr uint32_t PIDR4 = 0xfd0;
    static constexpr uint32_t PIDR0 = 0xfe0;
    static constexpr uint32_t PIDR1 = 0xfe4;
    static constexpr uint32_t PIDR2 = 0xfe8;
    static constexpr uint32_t PIDR3 = 0xfec;
    static constexpr uint32_t CIDR0 = 0xff0;
    static constexpr uint32_t CIDR1 = 0xff4;
    static constexpr uint32_t CIDR2 = 0xff8;
    static constexpr uint32_t CIDR3 = 0xffc;

    static constexpr uint32_t ICC_START = 1u << 0;
    static constexpr uint32_t ICIS_DONE = 1u << 0;

    std::array<uint8_t, REG_BYTES> m_regs{};
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

    void reset_registers()
    {
        m_regs.fill(0);

        store32(ICBC, p_build_config.get_value());
        store32(ICC, 0x00000000);
        store32(ICIS, 0x00000000);
        store32(ICIE, 0x00000000);
        store32(ICAE, 0x00000000);
        store32(ICIC, 0x00000000);
        store32(ICDA, 0x00000000);
        store32(ICDL, 0x00000000);
        store32(ICEVA, 0x00000000);
        store32(ICCVA, 0x00000000);
        for (uint32_t idx = 0; idx < 8; ++idx) {
            store32(ICCVAL_BASE + idx * sizeof(uint32_t), 0x00000000);
        }
        store32(PIDR4, 0x00000004);
        store32(PIDR0, 0x000000fd);
        store32(PIDR1, 0x000000b0);
        store32(PIDR2, 0x0000000b);
        store32(PIDR3, 0x00000000);
        store32(CIDR0, 0x0000000d);
        store32(CIDR1, 0x000000f0);
        store32(CIDR2, 0x00000005);
        store32(CIDR3, 0x000000b1);
    }

    bool is_read_only(uint32_t offset) const
    {
        return offset == ICBC || offset == ICIS ||
               (offset >= ICCVAL_BASE && offset < ICCVAL_BASE + 8 * sizeof(uint32_t)) ||
               offset == PIDR4 || offset == PIDR0 || offset == PIDR1 ||
               offset == PIDR2 || offset == PIDR3 || offset == CIDR0 ||
               offset == CIDR1 || offset == CIDR2 || offset == CIDR3;
    }

    void write32(uint32_t offset, uint32_t value)
    {
        switch (offset) {
        case ICIC:
            store32(ICIS, load32(ICIS) & ~value);
            store32(ICIC, value);
            break;
        case ICC:
            store32(ICC, value);
            if (value & ICC_START) {
                store32(ICIS, load32(ICIS) | ICIS_DONE);
            }
            break;
        default:
            if (!is_read_only(offset)) {
                store32(offset, value);
            }
            break;
        }
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) || offset + len > m_regs.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, &m_regs[offset], len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            if (len == sizeof(uint32_t) && (offset % sizeof(uint32_t)) == 0) {
                uint32_t value = 0;
                std::memcpy(&value, data, sizeof(value));
                write32(static_cast<uint32_t>(offset), value);
            } else {
                std::memcpy(&m_regs[offset], data, len);
            }
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trace_access(trans, offset, len, debug);

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset, unsigned int len, bool debug)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value()) {
            return;
        }

        ++m_trace_count;
        uint32_t value = 0;
        if (len <= sizeof(value)) {
            std::memcpy(&value, trans.get_data_ptr(), len);
        }

        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<uint32_t> p_build_config;
    tlm_utils::simple_target_socket<rse_integrity_checker, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit rse_integrity_checker(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_build_config("build_config", 0x00000109)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &rse_integrity_checker::b_transport);
        target_socket.register_transport_dbg(this, &rse_integrity_checker::transport_dbg);
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
