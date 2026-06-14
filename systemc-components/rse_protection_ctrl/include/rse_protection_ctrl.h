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

class rse_protection_ctrl : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x1000;
    static constexpr uint32_t STATUS = 0x000;
    static constexpr uint32_t CLEAR = 0x004;
    static constexpr uint32_t LOCK = 0x010;
    static constexpr uint32_t BLK_MAX = 0x014;
    static constexpr uint32_t BLK_CFG = 0x018;
    static constexpr uint32_t PIDR4 = 0xfd0;
    static constexpr uint32_t PIDR0 = 0xfe0;
    static constexpr uint32_t PIDR1 = 0xfe4;
    static constexpr uint32_t PIDR2 = 0xfe8;
    static constexpr uint32_t PIDR3 = 0xfec;
    static constexpr uint32_t CIDR0 = 0xff0;
    static constexpr uint32_t CIDR1 = 0xff4;
    static constexpr uint32_t CIDR2 = 0xff8;
    static constexpr uint32_t CIDR3 = 0xffc;

    static constexpr uint32_t STATUS_NONSECURE_DENY = 1u << 0;
    static constexpr uint32_t PROFILE_REGBANK = 0;
    static constexpr uint32_t PROFILE_SIE300_MPC = 1;

    using target_socket_type = tlm_utils::simple_target_socket_b<
        rse_protection_ctrl, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types,
        sc_core::SC_ZERO_OR_MORE_BOUND>;

    std::array<uint8_t, REG_BYTES> m_regs {};
    unsigned int m_trace_count = 0;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    static bool is_aligned32(uint64_t offset)
    {
        return (offset & 0x3u) == 0;
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

    bool locked() const
    {
        return p_lock_mask.get_value() != 0 &&
               (load32(LOCK) & p_lock_mask.get_value()) != 0;
    }

    bool is_read_only(uint32_t offset) const
    {
        if (p_profile.get_value() != PROFILE_SIE300_MPC) {
            return false;
        }

        return offset == BLK_MAX || offset == BLK_CFG || offset == PIDR4 ||
               offset == PIDR0 || offset == PIDR1 || offset == PIDR2 ||
               offset == PIDR3 || offset == CIDR0 || offset == CIDR1 ||
               offset == CIDR2 || offset == CIDR3;
    }

    void reset_registers()
    {
        m_regs.fill(0);

        if (p_profile.get_value() == PROFILE_SIE300_MPC) {
            store32(BLK_MAX, p_blk_max.get_value());
            store32(BLK_CFG, p_blk_cfg.get_value());
            store32(PIDR4, p_pidr4.get_value());
            store32(PIDR0, p_pidr0.get_value());
            store32(PIDR1, p_pidr1.get_value());
            store32(PIDR2, p_pidr2.get_value());
            store32(PIDR3, p_pidr3.get_value());
            store32(CIDR0, p_cidr0.get_value());
            store32(CIDR1, p_cidr1.get_value());
            store32(CIDR2, p_cidr2.get_value());
            store32(CIDR3, p_cidr3.get_value());
        }
    }

    void write32(uint32_t offset, uint32_t value)
    {
        if (p_error_latch.get_value() && offset == CLEAR) {
            store32(STATUS, load32(STATUS) & ~value);
            return;
        }

        if (p_lock_mask.get_value() != 0 && offset == LOCK) {
            store32(LOCK, load32(LOCK) | (value & p_lock_mask.get_value()));
            return;
        }

        if (locked() || is_read_only(offset)) {
            return;
        }

        store32(offset, value);
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug, bool non_secure)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) || offset + len > m_regs.size()) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (non_secure && trans.get_command() == tlm::TLM_WRITE_COMMAND &&
            !p_non_secure_writes.get_value()) {
            if (p_error_latch.get_value()) {
                store32(STATUS, load32(STATUS) | STATUS_NONSECURE_DENY);
            }
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            trace_access(trans, offset, len, debug, non_secure);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            std::memcpy(data, &m_regs[offset], len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            if (len == sizeof(uint32_t) && is_aligned32(offset)) {
                uint32_t value = 0;
                std::memcpy(&value, data, sizeof(value));
                write32(static_cast<uint32_t>(offset), value);
            } else if (!locked()) {
                std::memcpy(&m_regs[offset], data, len);
            }
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return false;
        }

        trace_access(trans, offset, len, debug, non_secure);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return true;
    }

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset,
                      unsigned int len, bool debug, bool non_secure)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value()) {
            return;
        }

        uint32_t value = 0;
        if (len <= sizeof(value)) {
            std::memcpy(&value, trans.get_data_ptr(), len);
        }

        ++m_trace_count;
        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (non_secure ? "ns_" : "s_")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<uint32_t> p_profile;
    cci::cci_param<uint32_t> p_blk_max;
    cci::cci_param<uint32_t> p_blk_cfg;
    cci::cci_param<uint32_t> p_lock_mask;
    cci::cci_param<bool> p_error_latch;
    cci::cci_param<bool> p_non_secure_writes;
    cci::cci_param<uint32_t> p_pidr4;
    cci::cci_param<uint32_t> p_pidr0;
    cci::cci_param<uint32_t> p_pidr1;
    cci::cci_param<uint32_t> p_pidr2;
    cci::cci_param<uint32_t> p_pidr3;
    cci::cci_param<uint32_t> p_cidr0;
    cci::cci_param<uint32_t> p_cidr1;
    cci::cci_param<uint32_t> p_cidr2;
    cci::cci_param<uint32_t> p_cidr3;

    target_socket_type target_socket;
    target_socket_type non_secure_socket;

    explicit rse_protection_ctrl(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_profile("profile", PROFILE_REGBANK)
        , p_blk_max("blk_max", 1)
        , p_blk_cfg("blk_cfg", 0x00000007)
        , p_lock_mask("lock_mask", 0)
        , p_error_latch("error_latch", false)
        , p_non_secure_writes("non_secure_writes", false)
        , p_pidr4("pidr4", 0x00000004)
        , p_pidr0("pidr0", 0x00000065)
        , p_pidr1("pidr1", 0x000000b8)
        , p_pidr2("pidr2", 0x0000000b)
        , p_pidr3("pidr3", 0x00000000)
        , p_cidr0("cidr0", 0x0000000d)
        , p_cidr1("cidr1", 0x000000f0)
        , p_cidr2("cidr2", 0x00000005)
        , p_cidr3("cidr3", 0x000000b1)
        , target_socket("target_socket")
        , non_secure_socket("non_secure_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &rse_protection_ctrl::b_transport);
        target_socket.register_transport_dbg(this, &rse_protection_ctrl::transport_dbg);
        non_secure_socket.register_b_transport(this, &rse_protection_ctrl::non_secure_b_transport);
        non_secure_socket.register_transport_dbg(this, &rse_protection_ctrl::non_secure_transport_dbg);
    }

    void before_end_of_elaboration() override
    {
        reset_registers();
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access(trans, false, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans, true, false) ? trans.get_data_length() : 0;
    }

    void non_secure_b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        (void)delay;
        trans.set_dmi_allowed(false);
        access(trans, false, true);
    }

    unsigned int non_secure_transport_dbg(tlm::tlm_generic_payload& trans)
    {
        return access(trans, true, true) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
