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
#include <string>

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
    static constexpr uint32_t DMAINFO_IIDR = 0xfc8;
    static constexpr uint32_t DMAINFO_AIDR = 0xfcc;
    static constexpr uint32_t CH_BASE = 0x1000;
    static constexpr uint32_t CH_STRIDE = 0x100;
    static constexpr uint32_t CH_CMD = 0x000;
    static constexpr uint32_t CH_STATUS = 0x004;
    static constexpr uint32_t CH_INTREN = 0x008;
    static constexpr uint32_t CH_CTRL = 0x00c;
    static constexpr uint32_t CH_SRCADDR = 0x010;
    static constexpr uint32_t CH_SRCADDRHI = 0x014;
    static constexpr uint32_t CH_DESADDR = 0x018;
    static constexpr uint32_t CH_DESADDRHI = 0x01c;
    static constexpr uint32_t CH_XSIZE = 0x020;
    static constexpr uint32_t CH_XSIZEHI = 0x024;
    static constexpr uint32_t CH_SRCTRANSCFG = 0x028;
    static constexpr uint32_t CH_DESTRANSCFG = 0x02c;
    static constexpr uint32_t CH_XADDRINC = 0x030;
    static constexpr uint32_t CH_FILLVAL = 0x038;

    static constexpr uint32_t CH_CMD_ENABLE = 0x00000001;
    static constexpr uint32_t CH_STATUS_INTR_DONE = 1u << 0;
    static constexpr uint32_t CH_STATUS_INTR_ERR = 1u << 1;
    static constexpr uint32_t CH_STATUS_STAT_DONE = 1u << 16;
    static constexpr uint32_t CH_STATUS_STAT_ERR = 1u << 17;
    static constexpr uint32_t CH_CTRL_XTYPE_SHIFT = 9;
    static constexpr uint32_t CH_CTRL_XTYPE_MASK = 0x7u << CH_CTRL_XTYPE_SHIFT;
    static constexpr uint32_t CH_CTRL_XTYPE_CONTINUE = 0x1u;
    static constexpr uint32_t CH_CTRL_XTYPE_FILL = 0x3u;
    static constexpr unsigned int FILL_CHUNK_BYTES = 256;
    static constexpr unsigned int COPY_CHUNK_BYTES = 1024;

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
        store32(DMAINFO_IIDR, 0x3a00043b); /* Arm DMA350 r0p0 */
        store32(DMAINFO_AIDR, 0x00000000);
        store32(CH_BASE + CH_CTRL, 0x00200200);
        store32(CH_BASE + CH_DESTRANSCFG, 0x000f0400);
    }

    bool mem_read(uint64_t address, uint8_t* data, unsigned int len, sc_core::sc_time& delay)
    {
        if (initiator_socket.size() == 0) {
            return false;
        }

        tlm::tlm_generic_payload trans;

        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(address);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);

        initiator_socket->b_transport(trans, delay);
        return trans.is_response_ok();
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

    static int16_t sign_extend16(uint32_t value)
    {
        return static_cast<int16_t>(value & 0xffffu);
    }

    static unsigned int transfer_bytes_from_ctrl(uint32_t ctrl)
    {
        return 1u << (ctrl & 0x7u);
    }

    uint64_t channel_address(uint32_t channel_base, uint32_t low_offset) const
    {
        return load32(channel_base + low_offset) |
               (static_cast<uint64_t>(load32(channel_base + low_offset + sizeof(uint32_t))) << 32);
    }

    uint32_t source_xsize(uint32_t channel_base) const
    {
        return (load32(channel_base + CH_XSIZE) & 0xffffu) |
               ((load32(channel_base + CH_XSIZEHI) & 0xffffu) << 16);
    }

    uint32_t dest_xsize(uint32_t channel_base) const
    {
        return ((load32(channel_base + CH_XSIZE) >> 16) & 0xffffu) |
               (((load32(channel_base + CH_XSIZEHI) >> 16) & 0xffffu) << 16);
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

    bool execute_copy(uint32_t channel_base, sc_core::sc_time& delay)
    {
        const uint64_t source_start = channel_address(channel_base, CH_SRCADDR);
        const uint64_t dest_start = channel_address(channel_base, CH_DESADDR);
        const uint32_t transfer_count = std::min(source_xsize(channel_base), dest_xsize(channel_base));
        const uint32_t xaddrinc = load32(channel_base + CH_XADDRINC);
        const int16_t source_inc = sign_extend16(xaddrinc);
        const int16_t dest_inc = sign_extend16(xaddrinc >> 16);
        const unsigned int transfer_bytes = transfer_bytes_from_ctrl(load32(channel_base + CH_CTRL));

        if (transfer_count == 0) {
            trace_copy(channel_base, source_start, dest_start, 0, true);
            return true;
        }

        if (source_inc == 1 && dest_inc == 1) {
            std::array<uint8_t, COPY_CHUNK_BYTES> chunk{};
            uint64_t source = source_start;
            uint64_t dest = dest_start;
            uint64_t bytes_remaining = static_cast<uint64_t>(transfer_count) * transfer_bytes;
            const uint64_t bytes_total = bytes_remaining;

            while (bytes_remaining != 0) {
                const auto len = static_cast<unsigned int>(
                    std::min<uint64_t>(bytes_remaining, chunk.size()));
                if (!mem_read(source, chunk.data(), len, delay) ||
                    !mem_write(dest, chunk.data(), len, delay)) {
                    trace_copy(channel_base, source_start, dest_start, bytes_total, false);
                    return false;
                }

                source += len;
                dest += len;
                bytes_remaining -= len;
            }

            trace_copy(channel_base, source_start, dest_start, bytes_total, true);
            return true;
        }

        std::array<uint8_t, 128> transfer{};
        int64_t source = static_cast<int64_t>(source_start);
        int64_t dest = static_cast<int64_t>(dest_start);
        for (uint32_t i = 0; i < transfer_count; ++i) {
            if (!mem_read(static_cast<uint64_t>(source), transfer.data(), transfer_bytes, delay) ||
                !mem_write(static_cast<uint64_t>(dest), transfer.data(), transfer_bytes, delay)) {
                trace_copy(channel_base, source_start, dest_start,
                           static_cast<uint64_t>(transfer_count) * transfer_bytes, false);
                return false;
            }

            source += static_cast<int64_t>(source_inc) * static_cast<int64_t>(transfer_bytes);
            dest += static_cast<int64_t>(dest_inc) * static_cast<int64_t>(transfer_bytes);
        }

        trace_copy(channel_base, source_start, dest_start,
                   static_cast<uint64_t>(transfer_count) * transfer_bytes, true);
        return true;
    }

    bool execute_channel(uint32_t channel_base, sc_core::sc_time& delay)
    {
        const uint32_t xtype =
            (load32(channel_base + CH_CTRL) & CH_CTRL_XTYPE_MASK) >> CH_CTRL_XTYPE_SHIFT;

        if (xtype == CH_CTRL_XTYPE_FILL) {
            return execute_fill(channel_base, delay);
        }

        return execute_copy(channel_base, delay);
    }

    void complete_channel(uint32_t channel_base, bool ok)
    {
        const uint32_t intren = load32(channel_base + CH_INTREN);
        uint32_t status = 0;

        if (ok) {
            status = CH_STATUS_STAT_DONE;
            if (intren & CH_STATUS_INTR_DONE) {
                status |= CH_STATUS_INTR_DONE;
            }
        } else {
            status = CH_STATUS_STAT_ERR;
            if (intren & CH_STATUS_INTR_ERR) {
                status |= CH_STATUS_INTR_ERR;
            }
        }

        store32(channel_base + CH_STATUS, status);
    }

    void clear_channel_status(uint32_t channel_base, uint32_t value)
    {
        store32(channel_base + CH_STATUS, load32(channel_base + CH_STATUS) & ~value);
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
            const uint32_t channel_base = offset - ch_offset;
            if (ch_offset == CH_STATUS) {
                clear_channel_status(channel_base, value);
                return;
            }

            if (ch_offset == CH_CMD) {
                /*
                 * BL1_1 polls STOP, CLEAR, and ENABLE commands until hardware
                 * clears them. This LT model completes those commands
                 * immediately.
                 */
                if (execute_side_effects && (value & CH_CMD_ENABLE) != 0) {
                    store32(channel_base + CH_STATUS, 0x00000000);
                    complete_channel(channel_base, execute_channel(channel_base, delay));
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
        if (!p_trace.get_value() ||
            m_trace_count >= p_trace_limit.get_value() ||
            !trace_access_filter_matches(offset)) {
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
        if (!trace_operation_filter_matches(channel_base, false)) {
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

    void trace_copy(uint32_t channel_base, uint64_t source, uint64_t dest, uint64_t bytes, bool ok)
    {
        if (!trace_operation_filter_matches(channel_base, true)) {
            return;
        }

        ++m_trace_count;
        std::cerr << name() << " copy"
                  << " channel=0x" << std::hex << ((channel_base - CH_BASE) / CH_STRIDE)
                  << " source=0x" << source
                  << " dest=0x" << dest
                  << " bytes=0x" << bytes
                  << " status=" << (ok ? "ok" : "error")
                  << std::dec << std::endl;
    }

    bool trace_access_filter_matches(uint64_t offset) const
    {
        const auto filter = p_trace_filter.get_value();

        if (filter == "operation") {
            return false;
        }

        if (offset < CH_BASE || offset >= REG_BYTES) {
            return p_trace_address_min.get_value() == 0 &&
                   (filter.empty() || filter == "all");
        }

        const uint32_t channel_base = static_cast<uint32_t>(
            CH_BASE + (((offset - CH_BASE) / CH_STRIDE) * CH_STRIDE));
        const uint32_t channel_offset = static_cast<uint32_t>((offset - CH_BASE) % CH_STRIDE);

        if (!trace_address_matches(channel_base)) {
            return false;
        }

        if (filter.empty() || filter == "all") {
            return true;
        }

        if (filter == "copy") {
            return is_copy_trace_offset(channel_offset);
        }

        if (filter == "fill") {
            return is_fill_trace_offset(channel_offset);
        }

        return false;
    }

    bool trace_operation_filter_matches(uint32_t channel_base, bool copy) const
    {
        if (!p_trace.get_value() ||
            m_trace_count >= p_trace_limit.get_value() ||
            !trace_address_matches(channel_base)) {
            return false;
        }

        const auto filter = p_trace_filter.get_value();
        if (filter.empty() || filter == "all" || filter == "operation") {
            return true;
        }

        return copy ? filter == "copy" : filter == "fill";
    }

    bool trace_address_matches(uint32_t channel_base) const
    {
        const auto min_address = p_trace_address_min.get_value();

        if (min_address == 0) {
            return true;
        }

        return channel_address(channel_base, CH_SRCADDR) >= min_address ||
               channel_address(channel_base, CH_DESADDR) >= min_address;
    }

    static bool is_copy_trace_offset(uint32_t channel_offset)
    {
        switch (channel_offset) {
        case CH_CMD:
        case CH_STATUS:
        case CH_CTRL:
        case CH_SRCADDR:
        case CH_SRCADDRHI:
        case CH_DESADDR:
        case CH_DESADDRHI:
        case CH_XSIZE:
        case CH_XSIZEHI:
        case CH_SRCTRANSCFG:
        case CH_DESTRANSCFG:
        case CH_XADDRINC:
            return true;
        default:
            return false;
        }
    }

    static bool is_fill_trace_offset(uint32_t channel_offset)
    {
        switch (channel_offset) {
        case CH_CMD:
        case CH_STATUS:
        case CH_CTRL:
        case CH_DESADDR:
        case CH_DESADDRHI:
        case CH_XSIZE:
        case CH_XSIZEHI:
        case CH_DESTRANSCFG:
        case CH_XADDRINC:
        case CH_FILLVAL:
            return true;
        default:
            return false;
        }
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<std::string> p_trace_filter;
    cci::cci_param<uint64_t> p_trace_address_min;
    initiator_socket_type initiator_socket;
    tlm_utils::simple_target_socket<dma350, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit dma350(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_trace_filter("trace_filter", "all")
        , p_trace_address_min("trace_address_min", 0)
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
