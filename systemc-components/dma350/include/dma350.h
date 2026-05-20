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

class dma350 : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x2000;
    static constexpr uint32_t DMASECINFO = 0xfb0;
    static constexpr uint32_t CH_BASE = 0x1000;
    static constexpr uint32_t CH_STRIDE = 0x100;
    static constexpr uint32_t CH_CMD = 0x000;
    static constexpr uint32_t CH_CTRL = 0x00c;
    static constexpr uint32_t CH_DESTRANSCFG = 0x02c;

    std::array<uint8_t, REG_BYTES> m_regs{};
    unsigned int m_trace_count = 0;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    void store32(uint32_t offset, uint32_t value)
    {
        std::memcpy(&m_regs[offset], &value, sizeof(value));
    }

    void reset_registers()
    {
        m_regs.fill(0);
        store32(DMASECINFO, 0x00000030); /* four DMA channels: ((value >> 4) & 0xf) + 1 */
        store32(CH_BASE + CH_CTRL, 0x00200200);
        store32(CH_BASE + CH_DESTRANSCFG, 0x000f0400);
    }

    void write32(uint32_t offset, uint32_t value)
    {
        if (offset >= CH_BASE && offset < REG_BYTES) {
            const uint32_t ch_offset = (offset - CH_BASE) % CH_STRIDE;
            if (ch_offset == CH_CMD) {
                /*
                 * BL1_1 polls STOP, CLEAR, and ENABLE commands until hardware
                 * clears them. This LT model completes those commands
                 * immediately.
                 */
                store32(offset, 0x00000000);
                return;
            }
        }

        store32(offset, value);
    }

    bool access(tlm::tlm_generic_payload& trans)
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

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        trace_access(trans, offset, len);
        return true;
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset, unsigned int len)
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
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    tlm_utils::simple_target_socket<dma350, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit dma350(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &dma350::b_transport);
        target_socket.register_transport_dbg(this, &dma350::transport_dbg);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access(trans);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
