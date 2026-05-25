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
#include <limits>
#include <string>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class rse_atu : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x1000;
    static constexpr uint32_t ATUBC = 0x000;
    static constexpr uint32_t ATUC = 0x004;
    static constexpr uint32_t ATUIS = 0x008;
    static constexpr uint32_t ATUIE = 0x00c;
    static constexpr uint32_t ATUIC = 0x010;
    static constexpr uint32_t ATUMA = 0x014;
    static constexpr uint32_t ATURSLA_BASE = 0x020;
    static constexpr uint32_t ATURELA_BASE = 0x0a0;
    static constexpr uint32_t ATURAV_L_BASE = 0x120;
    static constexpr uint32_t ATURAV_M_BASE = 0x1a0;
    static constexpr uint32_t ATUROBA_BASE = 0x220;
    static constexpr uint32_t ATURGPV_BASE = 0x2a0;
    static constexpr uint32_t REGION_COUNT = 32;
    static constexpr uint32_t REGION_STRIDE = sizeof(uint32_t);
    static constexpr uint32_t ATUBC_RC_OFF = 0u;
    static constexpr uint32_t ATUBC_RC_MASK = 0x7u << ATUBC_RC_OFF;
    static constexpr uint32_t ATUBC_PS_OFF = 4u;
    static constexpr uint32_t ATUBC_PS_MASK = 0xfu << ATUBC_PS_OFF;
    static constexpr uint32_t ATUIS_MISMATCH_ERROR = 0x1u;
    static constexpr uint32_t ATU_REGION_ROBA_MASK = 0xffffu;
    static constexpr uint32_t ATUROBA_AXPROT1_OFF = 2u;
    static constexpr uint32_t ATUROBA_AXNSE_OFF = 14u;
    static constexpr uint32_t ATU_ROBA_PASSTHROUGH = 0x0u;
    static constexpr uint32_t ATU_ROBA_RESERVED = 0x1u;
    static constexpr uint32_t ATU_ROBA_SET_0 = 0x2u;
    static constexpr uint32_t ATU_ROBA_SET_1 = 0x3u;

    std::array<uint8_t, REG_BYTES> m_regs{};
    unsigned int m_trace_count = 0;

    enum class translation_fault {
        none,
        unmapped,
        disabled,
        out_of_range,
        permission,
        overflow,
    };

    struct translation_info {
        uint32_t region = 0;
        uint64_t logical_start = 0;
        uint64_t logical_end = 0;
        uint64_t offset_pages = 0;
        uint64_t offset = 0;
        uint64_t physical = 0;
        translation_fault fault = translation_fault::none;
    };

    using initiator_socket_type = tlm_utils::simple_initiator_socket_b<
        rse_atu, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types,
        sc_core::SC_ZERO_OR_MORE_BOUND>;
    using target_socket_type = tlm_utils::simple_target_socket_b<
        rse_atu, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types,
        sc_core::SC_ZERO_OR_MORE_BOUND>;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    static bool is_region_array_offset(uint32_t offset)
    {
        return in_range(offset, ATURSLA_BASE) ||
               in_range(offset, ATURELA_BASE) ||
               in_range(offset, ATURAV_L_BASE) ||
               in_range(offset, ATURAV_M_BASE) ||
               in_range(offset, ATUROBA_BASE) ||
               in_range(offset, ATURGPV_BASE);
    }

    static bool in_range(uint32_t offset, uint32_t base)
    {
        return offset >= base &&
               offset < base + (REGION_COUNT * REGION_STRIDE) &&
               ((offset - base) % REGION_STRIDE) == 0;
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

    uint8_t page_shift() const
    {
        return static_cast<uint8_t>((load32(ATUBC) & ATUBC_PS_MASK) >>
                                    ATUBC_PS_OFF);
    }

    uint32_t supported_region_count() const
    {
        const uint32_t rc =
            (load32(ATUBC) & ATUBC_RC_MASK) >> ATUBC_RC_OFF;
        const uint32_t count = 1u << rc;

        return std::min(count, REGION_COUNT);
    }

    uint64_t region_offset_pages(uint32_t region) const
    {
        return (static_cast<uint64_t>(
                    load32(ATURAV_M_BASE + region * REGION_STRIDE)) << 32) |
               load32(ATURAV_L_BASE + region * REGION_STRIDE);
    }

    uint32_t region_start_page(uint32_t region) const
    {
        return load32(ATURSLA_BASE + region * REGION_STRIDE);
    }

    uint32_t region_end_page(uint32_t region) const
    {
        return load32(ATURELA_BASE + region * REGION_STRIDE);
    }

    uint32_t region_roba(uint32_t region) const
    {
        return load32(ATUROBA_BASE + region * REGION_STRIDE) &
               ATU_REGION_ROBA_MASK;
    }

    bool region_configured(uint32_t region) const
    {
        return region_start_page(region) != 0 || region_end_page(region) != 0 ||
               region_offset_pages(region) != 0 || region_roba(region) != 0 ||
               load32(ATURGPV_BASE + region * REGION_STRIDE) != 0;
    }

    static const char* fault_name(translation_fault fault)
    {
        switch (fault) {
        case translation_fault::none:
            return "none";
        case translation_fault::unmapped:
            return "unmapped";
        case translation_fault::disabled:
            return "disabled";
        case translation_fault::out_of_range:
            return "out_of_range";
        case translation_fault::permission:
            return "permission";
        case translation_fault::overflow:
            return "overflow";
        }

        return "unknown";
    }

    static bool roba_field_to_bit(uint32_t field, bool& bit)
    {
        switch (field & 0x3u) {
        case ATU_ROBA_PASSTHROUGH:
        case ATU_ROBA_SET_0:
            bit = false;
            return true;
        case ATU_ROBA_SET_1:
            bit = true;
            return true;
        case ATU_ROBA_RESERVED:
        default:
            return false;
        }
    }

    bool region_security_allowed(uint32_t region) const
    {
        const uint32_t roba = region_roba(region);
        bool nse = false;
        bool prot1 = false;

        if (!roba_field_to_bit(roba >> ATUROBA_AXNSE_OFF, nse) ||
            !roba_field_to_bit(roba >> ATUROBA_AXPROT1_OFF, prot1)) {
            return false;
        }

        /*
         * AMBA AXI security attribute: AxNSE/AxPROT[1] selects
         * secure, non-secure, root, or realm output PAS.
         */
        const uint32_t domain = (static_cast<uint32_t>(nse) << 1) |
                                static_cast<uint32_t>(prot1);
        return (p_permitted_security_domains.get_value() &
                (1u << domain)) != 0;
    }

    void reset_registers()
    {
        m_regs.fill(0);

        /*
         * TF-M decodes ATUBC[7:4] as page-size log2 and ATUBC[2:0] as
         * log2(region_count). 0xc5 gives 4 KiB pages and 32 regions.
         */
        store32(ATUBC, p_build_config.get_value());
    }

    void invalidate_translation_dmi()
    {
        if (!p_enable_dmi.get_value() || translation_socket.size() == 0) {
            return;
        }

        translation_socket->invalidate_direct_mem_ptr(
            0, std::numeric_limits<sc_dt::uint64>::max());
    }

    void write32(uint32_t offset, uint32_t value)
    {
        bool mapping_changed = false;

        switch (offset) {
        case ATUBC:
        case ATUIS:
        case ATUMA:
            break;
        case ATUIC:
            store32(ATUIS, load32(ATUIS) & ~value);
            store32(ATUIC, value);
            break;
        case ATUC:
        case ATUIE:
            store32(offset, value);
            mapping_changed = offset == ATUC;
            break;
        default:
            if (is_region_array_offset(offset)) {
                if (in_range(offset, ATUROBA_BASE)) {
                    value &= ATU_REGION_ROBA_MASK;
                }
                store32(offset, value);
                mapping_changed = true;
            } else {
                store32(offset, value);
            }
            break;
        }

        if (mapping_changed) {
            invalidate_translation_dmi();
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

    void latch_translation_fault(uint64_t logical)
    {
        store32(ATUIS, load32(ATUIS) | ATUIS_MISMATCH_ERROR);
        store32(ATUMA, static_cast<uint32_t>(logical));
    }

    bool translate_range(uint64_t logical, unsigned int len, translation_info& info,
                         bool latch_fault)
    {
        const uint8_t ps = page_shift();
        const uint32_t enabled = load32(ATUC);
        const uint64_t access_len = len == 0 ? 1 : len;
        translation_fault pending_fault = translation_fault::unmapped;

        for (uint32_t region = 0; region < supported_region_count(); ++region) {
            if (!region_configured(region)) {
                continue;
            }

            const uint64_t start =
                static_cast<uint64_t>(region_start_page(region)) << ps;
            const uint64_t end =
                (static_cast<uint64_t>(region_end_page(region)) + 1u) << ps;
            if (end <= start || logical < start || logical >= end) {
                continue;
            }

            if (access_len > end - logical) {
                pending_fault = translation_fault::out_of_range;
                continue;
            }

            if ((enabled & (1u << region)) == 0) {
                if (pending_fault == translation_fault::unmapped) {
                    pending_fault = translation_fault::disabled;
                }
                continue;
            }

            const uint64_t shifted_offset = region_offset_pages(region);
            info.region = region;
            info.logical_start = start;
            info.logical_end = end - 1;
            info.offset_pages = shifted_offset;
            if (shifted_offset >
                (std::numeric_limits<uint64_t>::max() >> ps)) {
                info.fault = translation_fault::overflow;
                if (latch_fault) {
                    latch_translation_fault(logical);
                }
                return false;
            }
            info.offset = shifted_offset << ps;
            if (!apply_region_offset(logical, info.offset, info.physical)) {
                info.fault = translation_fault::overflow;
                if (latch_fault) {
                    latch_translation_fault(logical);
                }
                return false;
            }
            if (!region_security_allowed(region)) {
                info.fault = translation_fault::permission;
                if (latch_fault) {
                    latch_translation_fault(logical);
                }
                return false;
            }
            info.fault = translation_fault::none;
            return true;
        }

        info.fault = pending_fault;
        if (latch_fault) {
            latch_translation_fault(logical);
        }
        return false;
    }

    bool translate_address(uint64_t logical, unsigned int len, uint64_t& physical)
    {
        translation_info info;
        if (!translate_range(logical, len, info, true)) {
            return false;
        }

        physical = info.physical;
        return true;
    }

    bool forward_translation(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay,
                             bool debug)
    {
        if (initiator_socket.size() == 0) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        const uint64_t logical = trans.get_address();
        const unsigned int len = trans.get_data_length();
        translation_info info;

        if (trans.get_data_ptr() == nullptr || len == 0 ||
            !translate_range(logical, len, info, true)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            trace_translation(trans, logical, info, len, false, debug);
            return false;
        }

        trans.set_address(info.physical);
        if (debug) {
            initiator_socket->transport_dbg(trans);
        } else {
            initiator_socket->b_transport(trans, delay);
        }
        trans.set_address(logical);
        trace_translation(trans, logical, info, len, trans.is_response_ok(), debug);
        return trans.is_response_ok();
    }

    static uint64_t range_end(uint64_t start, unsigned int len)
    {
        if (len == 0) {
            return start;
        }

        const uint64_t span = static_cast<uint64_t>(len) - 1;
        if (start > std::numeric_limits<uint64_t>::max() - span) {
            return std::numeric_limits<uint64_t>::max();
        }

        return start + span;
    }

    static bool offset_is_negative(uint64_t offset)
    {
        return (offset & (uint64_t{1} << 63)) != 0;
    }

    static uint64_t negative_offset_delta(uint64_t offset)
    {
        return (~offset) + 1;
    }

    static bool apply_region_offset(uint64_t logical, uint64_t offset,
                                    uint64_t& physical)
    {
        if (offset_is_negative(offset)) {
            const uint64_t delta = negative_offset_delta(offset);
            if (logical < delta) {
                return false;
            }
            physical = logical - delta;
            return true;
        }

        if (logical > std::numeric_limits<uint64_t>::max() - offset) {
            return false;
        }

        physical = logical + offset;
        return true;
    }

    static bool token_matches(const std::string& filter, const std::string& token)
    {
        size_t start = 0;
        while (start <= filter.size()) {
            const size_t comma = filter.find(',', start);
            const size_t end = comma == std::string::npos ? filter.size() : comma;
            if (filter.substr(start, end - start) == token) {
                return true;
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }

        return false;
    }

    bool trace_category_matches(const std::string& category,
                                tlm::tlm_command command) const
    {
        const auto filter = p_trace_filter.get_value();

        if (filter.empty() || filter == "all") {
            return true;
        }

        if (token_matches(filter, category)) {
            return true;
        }

        if (command == tlm::TLM_READ_COMMAND) {
            return token_matches(filter, "read");
        }
        if (command == tlm::TLM_WRITE_COMMAND) {
            return token_matches(filter, "write");
        }

        return false;
    }

    bool trace_range_matches(uint64_t start, uint64_t end) const
    {
        const auto min_address = p_trace_address_min.get_value();
        const auto max_address = p_trace_address_max.get_value();

        if (min_address == 0 && max_address == 0) {
            return true;
        }

        if (end < min_address) {
            return false;
        }

        if (max_address != 0 && start > max_address) {
            return false;
        }

        return true;
    }

    bool trace_address_matches(uint64_t address, unsigned int len) const
    {
        return trace_range_matches(address, range_end(address, len));
    }

    bool trace_translation_address_matches(uint64_t logical, unsigned int len,
                                           const translation_info& info,
                                           bool translated) const
    {
        if (trace_address_matches(logical, len)) {
            return true;
        }

        return translated && trace_address_matches(info.physical, len);
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset, unsigned int len, bool debug)
    {
        if (!p_trace.get_value() ||
            m_trace_count >= p_trace_limit.get_value() ||
            !trace_category_matches("register", trans.get_command()) ||
            !trace_address_matches(offset, len)) {
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

    void trace_translation(tlm::tlm_generic_payload& trans, uint64_t logical,
                           const translation_info& info, unsigned int len, bool ok,
                           bool debug)
    {
        if (!p_trace.get_value() ||
            m_trace_count >= p_trace_limit.get_value() ||
            !trace_category_matches("translation", trans.get_command()) ||
            !trace_translation_address_matches(logical, len, info, ok)) {
            return;
        }

        ++m_trace_count;
        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "translate_read" : "translate_write")
                  << " logical=0x" << std::hex << logical
                  << " physical=0x" << info.physical
                  << " len=0x" << len
                  << " status=" << (ok ? "ok" : "error")
                  << " reason=" << (ok ? "none" : fault_name(info.fault))
                  << " region=" << std::dec << info.region
                  << " offset_pages=0x" << std::hex << info.offset_pages
                  << std::dec << std::endl;
    }

    void trace_dmi(tlm::tlm_generic_payload& trans, uint64_t logical,
                   const translation_info& info, unsigned int len, bool translated,
                   bool ok, const char* reason, uint64_t downstream_start = 0,
                   uint64_t downstream_end = 0, uint64_t upstream_start = 0,
                   uint64_t upstream_end = 0)
    {
        if (!p_trace.get_value() ||
            m_trace_count >= p_trace_limit.get_value() ||
            !trace_category_matches("dmi", trans.get_command()) ||
            !trace_translation_address_matches(logical, len, info, translated)) {
            return;
        }

        ++m_trace_count;
        std::cerr << name() << " dmi"
                  << " logical=0x" << std::hex << logical
                  << " physical=0x" << (translated ? info.physical : 0)
                  << " len=0x" << len
                  << " status=" << (ok ? "ok" : "error")
                  << " reason=" << reason
                  << " region=" << std::dec << info.region
                  << " upstream=0x" << std::hex << upstream_start
                  << "-0x" << upstream_end
                  << " downstream=0x" << downstream_start
                  << "-0x" << downstream_end
                  << std::dec << std::endl;
    }

    void invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end)
    {
        (void)start;
        (void)end;
        invalidate_translation_dmi();
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<std::string> p_trace_filter;
    cci::cci_param<uint64_t> p_trace_address_min;
    cci::cci_param<uint64_t> p_trace_address_max;
    cci::cci_param<uint32_t> p_build_config;
    cci::cci_param<uint32_t> p_permitted_security_domains;
    cci::cci_param<bool> p_enable_dmi;
    initiator_socket_type initiator_socket;
    target_socket_type target_socket;
    target_socket_type translation_socket;

    explicit rse_atu(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_trace_filter("trace_filter", "all")
        , p_trace_address_min("trace_address_min", 0)
        , p_trace_address_max("trace_address_max", 0)
        , p_build_config("build_config", 0x000000c5)
        , p_permitted_security_domains("permitted_security_domains", 0xf)
        , p_enable_dmi("enable_dmi", false)
        , initiator_socket("initiator_socket")
        , target_socket("target_socket")
        , translation_socket("translation_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &rse_atu::b_transport);
        target_socket.register_transport_dbg(this, &rse_atu::transport_dbg);
        translation_socket.register_b_transport(this, &rse_atu::translation_b_transport);
        translation_socket.register_transport_dbg(this, &rse_atu::translation_transport_dbg);
        translation_socket.register_get_direct_mem_ptr(this, &rse_atu::translation_get_direct_mem_ptr);
        initiator_socket.register_invalidate_direct_mem_ptr(this, &rse_atu::invalidate_direct_mem_ptr);
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

    void translation_b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        trans.set_dmi_allowed(false);
        forward_translation(trans, delay, false);
    }

    unsigned int translation_transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        return forward_translation(trans, delay, true) ? trans.get_data_length() : 0;
    }

    bool translation_get_direct_mem_ptr(tlm::tlm_generic_payload& trans,
                                        tlm::tlm_dmi& dmi_data)
    {
        const uint64_t logical = trans.get_address();
        const unsigned int len = trans.get_data_length();
        translation_info info;

        if (initiator_socket.size() == 0) {
            trace_dmi(trans, logical, info, len, false, false, "unbound");
            return false;
        }
        if (!p_enable_dmi.get_value()) {
            trace_dmi(trans, logical, info, len, false, false, "disabled");
            return false;
        }

        if (!translate_range(logical, len, info, false)) {
            trace_dmi(trans, logical, info, len, false, false,
                      fault_name(info.fault));
            return false;
        }

        trans.set_address(info.physical);
        const bool granted = initiator_socket->get_direct_mem_ptr(trans, dmi_data);
        trans.set_address(logical);
        if (!granted) {
            trace_dmi(trans, logical, info, len, true, false, "target_denied");
            return false;
        }

        const uint64_t downstream_start = dmi_data.get_start_address();
        const uint64_t downstream_end = dmi_data.get_end_address();
        const bool negative_offset = offset_is_negative(info.offset);
        uint64_t logical_start = 0;
        uint64_t logical_end = 0;

        if (negative_offset) {
            const uint64_t delta = negative_offset_delta(info.offset);
            if (downstream_start >
                std::numeric_limits<uint64_t>::max() - delta) {
                trace_dmi(trans, logical, info, len, true, false,
                          "invalid_range", downstream_start, downstream_end);
                return false;
            }
            logical_start = downstream_start + delta;
            logical_end = downstream_end >
                                  std::numeric_limits<uint64_t>::max() - delta
                              ? std::numeric_limits<uint64_t>::max()
                              : downstream_end + delta;
        } else {
            if (downstream_end < info.offset) {
                trace_dmi(trans, logical, info, len, true, false,
                          "invalid_range", downstream_start, downstream_end);
                return false;
            }
            logical_start = downstream_start < info.offset
                                ? 0
                                : downstream_start - info.offset;
            logical_end = downstream_end - info.offset;
        }
        if (logical_start > logical_end) {
            trace_dmi(trans, logical, info, len, true, false, "invalid_range",
                      downstream_start, downstream_end);
            return false;
        }

        const uint64_t upstream_start = std::max(logical_start, info.logical_start);
        const uint64_t upstream_end = std::min(logical_end, info.logical_end);
        const uint64_t request_end = range_end(logical, len);
        if (upstream_start > upstream_end || logical < upstream_start ||
            request_end > upstream_end) {
            trace_dmi(trans, logical, info, len, true, false, "outside_region",
                      downstream_start, downstream_end, upstream_start,
                      upstream_end);
            return false;
        }

        if (!negative_offset && upstream_start >
            std::numeric_limits<uint64_t>::max() - info.offset) {
            trace_dmi(trans, logical, info, len, true, false, "overflow",
                      downstream_start, downstream_end, upstream_start,
                      upstream_end);
            return false;
        }
        uint64_t physical_start = 0;
        if (!apply_region_offset(upstream_start, info.offset, physical_start)) {
            trace_dmi(trans, logical, info, len, true, false, "overflow",
                      downstream_start, downstream_end, upstream_start,
                      upstream_end);
            return false;
        }
        if (physical_start < downstream_start) {
            trace_dmi(trans, logical, info, len, true, false, "invalid_range",
                      downstream_start, downstream_end, upstream_start,
                      upstream_end);
            return false;
        }
        dmi_data.set_dmi_ptr(dmi_data.get_dmi_ptr() +
                             (physical_start - downstream_start));
        dmi_data.set_start_address(upstream_start);
        dmi_data.set_end_address(upstream_end);
        trace_dmi(trans, logical, info, len, true, true, "granted",
                  downstream_start, downstream_end, upstream_start, upstream_end);
        return true;
    }
};

extern "C" void module_register();
