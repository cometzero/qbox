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

class rse_sam : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x1000;
    static constexpr uint32_t SAMBC = 0x000;
    static constexpr uint32_t SAMES_BASE = 0x004;
    static constexpr uint32_t SAMECL_BASE = 0x00c;
    static constexpr uint32_t SAMEM_BASE = 0x014;
    static constexpr uint32_t SAMIM_BASE = 0x01c;
    static constexpr uint32_t SAMRRLS_BASE = 0x024;
    static constexpr uint32_t SAMEC_BASE = 0x044;
    static constexpr uint32_t SAMECTIV = 0x064;
    static constexpr uint32_t SAMWDCIV = 0x068;
    static constexpr uint32_t SAMRL = 0x06c;
    static constexpr uint32_t SAMICV = 0x070;
    static constexpr uint32_t SAMCDRES = 0x074;
    static constexpr uint32_t SAMRRES_BASE = 0x078;
    static constexpr uint32_t VMPWCA_BASE = 0x084;
    static constexpr uint32_t VMSCEECA_BASE = 0x094;
    static constexpr uint32_t VMDUEECA_BASE = 0x0a4;
    static constexpr uint32_t TRAMSCEECA = 0x0b4;
    static constexpr uint32_t TRAMDUEECA = 0x0b8;
    static constexpr uint32_t PIDR4 = 0xfd0;
    static constexpr uint32_t PIDR0 = 0xfe0;
    static constexpr uint32_t PIDR1 = 0xfe4;
    static constexpr uint32_t PIDR2 = 0xfe8;
    static constexpr uint32_t PIDR3 = 0xfec;
    static constexpr uint32_t CIDR0 = 0xff0;
    static constexpr uint32_t CIDR1 = 0xff4;
    static constexpr uint32_t CIDR2 = 0xff8;
    static constexpr uint32_t CIDR3 = 0xffc;

    static constexpr uint32_t WORD_BYTES = sizeof(uint32_t);
    static constexpr uint32_t SAM_EVENT_WORDS = 2;
    static constexpr uint32_t SAM_RESPONSE_WORDS = 8;
    static constexpr uint32_t SAM_RAS_WORDS = 3;
    static constexpr uint32_t SAM_VM_WORDS = 4;

    std::array<uint8_t, REG_BYTES> m_regs{};
    unsigned int m_trace_count = 0;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    static bool in_words(uint32_t offset, uint32_t base, uint32_t words)
    {
        return offset >= base &&
               offset < base + words * WORD_BYTES &&
               ((offset - base) % WORD_BYTES) == 0;
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

        store32(SAMBC, p_build_config.get_value());
        store32(PIDR4, 0x00000004);
        store32(PIDR0, 0x000000f4);
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
        return offset == SAMBC ||
               in_words(offset, SAMES_BASE, SAM_EVENT_WORDS) ||
               offset == PIDR4 || offset == PIDR0 || offset == PIDR1 ||
               offset == PIDR2 || offset == PIDR3 || offset == CIDR0 ||
               offset == CIDR1 || offset == CIDR2 || offset == CIDR3;
    }

    void write32(uint32_t offset, uint32_t value)
    {
        if (in_words(offset, SAMECL_BASE, SAM_EVENT_WORDS)) {
            const uint32_t status_offset = SAMES_BASE + (offset - SAMECL_BASE);
            store32(status_offset, load32(status_offset) & ~value);
            store32(offset, value);
            return;
        }

        if (!is_read_only(offset)) {
            store32(offset, value);
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
    tlm_utils::simple_target_socket<rse_sam, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit rse_sam(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_build_config("build_config", 0x00000700)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &rse_sam::b_transport);
        target_socket.register_transport_dbg(this, &rse_sam::transport_dbg);
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
