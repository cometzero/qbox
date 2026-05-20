/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class dma350 : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x2000;
    static constexpr uint32_t DMASECINFO = 0xfb0;
    static constexpr uint32_t CH_BASE = 0x1000;
    static constexpr uint32_t CH_STRIDE = 0x100;
    static constexpr uint32_t CH_CMD = 0x000;
    static constexpr uint32_t CH_CTRL = 0x00c;
    static constexpr uint32_t CH_DESADDR = 0x018;
    static constexpr uint32_t CH_XSIZE = 0x020;
    static constexpr uint32_t CH_DESTRANSCFG = 0x02c;
    static constexpr uint32_t CH_XADDRINC = 0x030;
    static constexpr uint32_t CH_FILLVAL = 0x038;

    static constexpr uint32_t CH_CMD_ENABLE = 0x00000001;
    static constexpr unsigned int FILL_CHUNK_BYTES = 256;

    std::array<uint8_t, REG_BYTES> m_regs{};
    unsigned int m_trace_count = 0;

    using initiator_socket_type = tlm_utils::simple_initiator_socket_b<
        dma350, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types, sc_core::SC_ZERO_OR_MORE_BOUND>;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    void store32(uint32_t offset, uint32_t value)
    {
        std::memcpy(&m_regs[offset], &value, sizeof(value));
    }

    uint32_t load32(uint32_t offset) const
    {
        uint32_t value = 0;
        std::memcpy(&value, &m_regs[offset], sizeof(value));
        return value;
    }

    void reset_registers()
    {
        m_regs.fill(0);
        store32(DMASECINFO, 0x00000030); /* four DMA channels: ((value >> 4) & 0xf) + 1 */
        store32(CH_BASE + CH_CTRL, 0x00200200);
        store32(CH_BASE + CH_DESTRANSCFG, 0x000f0400);
    }

    bool mem_write(uint64_t address, const uint8_t* data, unsigned int len, sc_core::sc_time& delay)
    {
        if (initiator_socket.size() == 0) {
            return false;
        }

        tlm::tlm_generic_payload trans;

        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(address);
        trans.set_data_ptr(const_cast<uint8_t*>(data));
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);

        initiator_socket->b_transport(trans, delay);
        return trans.is_response_ok();
    }

    template <size_t N>
    void fill_chunk(std::array<uint8_t, N>& chunk, uint32_t fill_value)
    {
        for (auto i = 0u; i < chunk.size(); ++i) {
            chunk[i] = static_cast<uint8_t>(fill_value >> ((i & 0x3u) * 8));
        }
    }

    bool execute_fill(uint32_t channel_base, sc_core::sc_time& delay)
    {
        const uint64_t dest = load32(channel_base + CH_DESADDR);
        const uint32_t xsize = load32(channel_base + CH_XSIZE);
        const uint32_t xaddrinc = load32(channel_base + CH_XADDRINC);
        const uint32_t fill_value = load32(channel_base + CH_FILLVAL);
        const uint32_t transfer_count = (xsize >> 16) & 0xffffu;
        const int16_t dest_inc = static_cast<int16_t>((xaddrinc >> 16) & 0xffffu);
        constexpr unsigned int transfer_bytes = sizeof(uint64_t);

        if (transfer_count == 0) {
            return true;
        }

        const uint64_t transfer_bytes_total = static_cast<uint64_t>(transfer_count) * transfer_bytes;
        if (dest_inc != 1) {
            const bool ok = execute_strided_fill(dest, transfer_count, dest_inc, fill_value, delay);
            trace_fill(channel_base, dest, transfer_bytes_total, ok);
            return ok;
        }

        std::array<uint8_t, FILL_CHUNK_BYTES> chunk{};
        fill_chunk(chunk, fill_value);

        uint64_t address = dest;
        uint64_t bytes_remaining = transfer_bytes_total;
        while (bytes_remaining != 0) {
            const auto len = static_cast<unsigned int>(
                std::min<uint64_t>(bytes_remaining, chunk.size()));
            if (!mem_write(address, chunk.data(), len, delay)) {
                trace_fill(channel_base, dest, transfer_bytes_total, false);
                return false;
            }

            address += len;
            bytes_remaining -= len;
        }

        trace_fill(channel_base, dest, transfer_bytes_total, true);
        return true;
    }

    bool execute_strided_fill(uint64_t dest, uint32_t transfer_count, int16_t dest_inc,
                              uint32_t fill_value, sc_core::sc_time& delay)
    {
        std::array<uint8_t, sizeof(uint64_t)> data{};
        fill_chunk(data, fill_value);

        int64_t address = static_cast<int64_t>(dest);
        for (uint32_t i = 0; i < transfer_count; ++i) {
            if (!mem_write(static_cast<uint64_t>(address), data.data(), data.size(), delay)) {
                return false;
            }

            address += static_cast<int64_t>(dest_inc) * static_cast<int64_t>(data.size());
        }

        return true;
    }

    void write32(uint32_t offset, uint32_t value, sc_core::sc_time& delay, bool execute_side_effects)
    {
        if (offset >= CH_BASE && offset < REG_BYTES) {
            const uint32_t ch_offset = (offset - CH_BASE) % CH_STRIDE;
            if (ch_offset == CH_CMD) {
                /*
                 * BL1_1 polls STOP, CLEAR, and ENABLE commands until hardware
                 * clears them. This LT model completes those commands
                 * immediately.
                 */
                if (execute_side_effects && (value & CH_CMD_ENABLE) != 0) {
                    execute_fill(offset - ch_offset, delay);
                }
                store32(offset, 0x00000000);
                return;
            }
        }

        store32(offset, value);
    }

    bool access(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay, bool execute_side_effects)
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
                write32(static_cast<uint32_t>(offset), value, delay, execute_side_effects);
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

    void trace_fill(uint32_t channel_base, uint64_t dest, uint64_t bytes, bool ok)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value()) {
            return;
        }

        ++m_trace_count;
        std::cerr << name() << " fill"
                  << " channel=0x" << std::hex << ((channel_base - CH_BASE) / CH_STRIDE)
                  << " dest=0x" << dest
                  << " bytes=0x" << bytes
                  << " status=" << (ok ? "ok" : "error")
                  << std::dec << std::endl;
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    initiator_socket_type initiator_socket;
    tlm_utils::simple_target_socket<dma350, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit dma350(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , initiator_socket("initiator_socket")
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &dma350::b_transport);
        target_socket.register_transport_dbg(this, &dma350::transport_dbg);
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        trans.set_dmi_allowed(false);
        access(trans, delay, true);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        return access(trans, delay, false) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
