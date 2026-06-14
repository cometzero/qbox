/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class zena_ssu : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x1000;
    static constexpr uint32_t ERR_FR = 0x000;
    static constexpr uint32_t ERR_CTRL = 0x008;
    static constexpr uint32_t ERR_STATUS = 0x010;
    static constexpr uint32_t ERR_IMPDEF = 0x800;
    static constexpr uint32_t SYS_KEY = 0x804;
    static constexpr uint32_t SYS_STATUS = 0x808;
    static constexpr uint32_t SYS_CTRL = 0x810;
    static constexpr uint32_t STATUS_DETAIL = 0x814;
    static constexpr uint32_t PIDR4 = 0xfd0;
    static constexpr uint32_t PIDR0 = 0xfe0;
    static constexpr uint32_t PIDR1 = 0xfe4;
    static constexpr uint32_t PIDR2 = 0xfe8;
    static constexpr uint32_t PIDR3 = 0xfec;
    static constexpr uint32_t CIDR0 = 0xff0;
    static constexpr uint32_t CIDR1 = 0xff4;
    static constexpr uint32_t CIDR2 = 0xff8;
    static constexpr uint32_t CIDR3 = 0xffc;

    static constexpr uint32_t SYS_KEY_VALUE = 0xbe;
    static constexpr uint32_t ERR_FR_RESET = 0x2;
    static constexpr uint32_t ERR_CTRL_ED = 1u << 0;
    static constexpr uint32_t ERR_STATUS_SERR_MASK = 0x000000ff;
    static constexpr uint32_t ERR_STATUS_IERR_MASK = 0x00001f00;
    static constexpr uint32_t ERR_STATUS_OF = 1u << 27;
    static constexpr uint32_t ERR_STATUS_V = 1u << 30;
    static constexpr uint32_t ERR_STATUS_W1C_MASK = ERR_STATUS_V | ERR_STATUS_OF;
    static constexpr uint32_t ERR_STATUS_RW_WHEN_VALID_MASK = ERR_STATUS_SERR_MASK | ERR_STATUS_IERR_MASK;
    static constexpr uint32_t ERR_STATUS_IERR_INC_SEQ = 1u << 9;
    static constexpr uint32_t ERR_STATUS_IERR_ERR_IN = 1u << 12;
    static constexpr uint32_t ERR_STATUS_SERR_SW = 1u << 0;

    static constexpr uint32_t IMPDEF_CR_EN = 1u << 0;
    static constexpr uint32_t IMPDEF_NCR_EN = 1u << 1;
    static constexpr uint32_t IMPDEF_APB_SW = 1u << 4;
    static constexpr uint32_t IMPDEF_INC_SEQ = 1u << 5;
    static constexpr uint32_t IMPDEF_APB_PROT = 1u << 6;
    static constexpr uint32_t IMPDEF_RW_MASK =
        IMPDEF_CR_EN | IMPDEF_NCR_EN | IMPDEF_APB_SW | IMPDEF_INC_SEQ | IMPDEF_APB_PROT;

    static constexpr uint32_t SYS_STATUS_TEST = 1u << 0;
    static constexpr uint32_t SYS_STATUS_SAFE = 1u << 1;
    static constexpr uint32_t SYS_STATUS_ERRN = 1u << 2;
    static constexpr uint32_t SYS_STATUS_ERRC = 1u << 3;

    uint32_t m_err_ctrl = ERR_CTRL_ED;
    uint32_t m_err_status = 0;
    uint32_t m_err_impdef = IMPDEF_CR_EN | IMPDEF_APB_SW | IMPDEF_INC_SEQ | IMPDEF_APB_PROT;
    uint32_t m_sys_status = SYS_STATUS_TEST;
    uint32_t m_status_detail = 0;
    bool m_unlocked = false;
    unsigned int m_trace_count = 0;
    bool m_safety_status = false;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    static bool is_aligned32(uint64_t offset)
    {
        return (offset & 0x3u) == 0;
    }

    bool require_unlock()
    {
        if (!p_enforce_sys_key.get_value()) {
            return true;
        }

        if (m_unlocked) {
            m_unlocked = false;
            return true;
        }

        set_sequence_error();
        return false;
    }

    void set_sequence_error()
    {
        if ((m_err_impdef & IMPDEF_INC_SEQ) != 0) {
            m_err_status |= ERR_STATUS_V | ERR_STATUS_IERR_INC_SEQ | ERR_STATUS_SERR_SW;
        }
        update_output();
    }

    void set_fault(bool critical)
    {
        if ((m_err_ctrl & ERR_CTRL_ED) == 0) {
            return;
        }

        if (critical && (m_err_impdef & IMPDEF_CR_EN) == 0) {
            return;
        }

        if (!critical && (m_err_impdef & IMPDEF_NCR_EN) == 0) {
            return;
        }

        m_err_status |= ERR_STATUS_V | ERR_STATUS_IERR_ERR_IN | ERR_STATUS_SERR_SW;
        m_sys_status = critical ? SYS_STATUS_ERRC : SYS_STATUS_ERRN;
        update_output();
    }

    void write_status(uint32_t value)
    {
        m_err_status &= ~(value & ERR_STATUS_W1C_MASK);

        if (m_err_status & ERR_STATUS_V) {
            m_err_status = (m_err_status & ~ERR_STATUS_RW_WHEN_VALID_MASK) |
                           (value & ERR_STATUS_RW_WHEN_VALID_MASK);
        }

        if ((m_err_status & ERR_STATUS_V) == 0 &&
            (m_sys_status == SYS_STATUS_ERRC || m_sys_status == SYS_STATUS_ERRN)) {
            m_sys_status = SYS_STATUS_SAFE;
        }

        update_output();
    }

    void write_sys_ctrl(uint32_t value)
    {
        switch (value & 0x3u) {
        case 0:
            m_sys_status = SYS_STATUS_TEST;
            break;
        case 1:
            m_sys_status = SYS_STATUS_SAFE;
            break;
        case 2:
            m_sys_status = SYS_STATUS_ERRN;
            break;
        case 3:
            m_sys_status = SYS_STATUS_ERRC;
            break;
        default:
            break;
        }

        update_output();
    }

    uint32_t pidcid_value(uint32_t offset) const
    {
        switch (offset) {
        case PIDR4:
            return p_pidr4.get_value();
        case PIDR0:
            return p_pidr0.get_value();
        case PIDR1:
            return p_pidr1.get_value();
        case PIDR2:
            return p_pidr2.get_value();
        case PIDR3:
            return p_pidr3.get_value();
        case CIDR0:
            return p_cidr0.get_value();
        case CIDR1:
            return p_cidr1.get_value();
        case CIDR2:
            return p_cidr2.get_value();
        case CIDR3:
            return p_cidr3.get_value();
        default:
            return 0;
        }
    }

    uint32_t read32(uint32_t offset) const
    {
        switch (offset) {
        case ERR_FR:
            return ERR_FR_RESET;
        case ERR_CTRL:
            return m_err_ctrl;
        case ERR_STATUS:
            return m_err_status;
        case ERR_IMPDEF:
            return m_err_impdef;
        case SYS_KEY:
            return m_unlocked ? SYS_KEY_VALUE : 0;
        case SYS_STATUS:
            return m_sys_status;
        case SYS_CTRL:
            return 0;
        case STATUS_DETAIL:
            return m_status_detail;
        default:
            return pidcid_value(offset);
        }
    }

    void write32(uint32_t offset, uint32_t value)
    {
        if (offset == SYS_KEY) {
            m_unlocked = (value == SYS_KEY_VALUE);
            if (!m_unlocked) {
                set_sequence_error();
            }
            return;
        }

        if (!require_unlock()) {
            return;
        }

        switch (offset) {
        case ERR_CTRL:
            m_err_ctrl = value & ERR_CTRL_ED;
            update_output();
            break;
        case ERR_STATUS:
            write_status(value);
            break;
        case ERR_IMPDEF:
            m_err_impdef = (m_err_impdef & ~IMPDEF_RW_MASK) | (value & IMPDEF_RW_MASK);
            update_output();
            break;
        case SYS_CTRL:
            write_sys_ctrl(value);
            break;
        case STATUS_DETAIL:
            m_status_detail = value & 0x0000ffffu;
            break;
        default:
            break;
        }
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) || offset + len > REG_BYTES) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            uint64_t remaining = len;
            uint64_t cur = offset;
            uint8_t* out = data;
            while (remaining > 0) {
                const uint32_t value = read32(static_cast<uint32_t>(cur & ~0x3ull));
                const unsigned int shift = static_cast<unsigned int>(cur & 0x3ull);
                const unsigned int chunk = std::min<uint64_t>(remaining, 4 - shift);
                std::memcpy(out, reinterpret_cast<const uint8_t*>(&value) + shift, chunk);
                remaining -= chunk;
                cur += chunk;
                out += chunk;
            }
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            if (len == sizeof(uint32_t) && is_aligned32(offset)) {
                uint32_t value = 0;
                std::memcpy(&value, data, sizeof(value));
                write32(static_cast<uint32_t>(offset), value);
            }
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
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" : "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

    void reset_registers()
    {
        m_err_ctrl = ERR_CTRL_ED;
        m_err_status = 0;
        m_err_impdef = IMPDEF_CR_EN | IMPDEF_APB_SW | IMPDEF_INC_SEQ | IMPDEF_APB_PROT;
        m_sys_status = SYS_STATUS_TEST;
        m_status_detail = 0;
        m_unlocked = false;
        m_safety_status = false;
        update_output();
    }

    void update_output()
    {
        const bool value = (m_sys_status == SYS_STATUS_ERRC || m_sys_status == SYS_STATUS_ERRN);
        if (value == m_safety_status) {
            return;
        }

        m_safety_status = value;
        if (safety_status.size() != 0) {
            safety_status->write(m_safety_status);
        }
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<bool> p_enforce_sys_key;
    cci::cci_param<uint32_t> p_pidr4;
    cci::cci_param<uint32_t> p_pidr0;
    cci::cci_param<uint32_t> p_pidr1;
    cci::cci_param<uint32_t> p_pidr2;
    cci::cci_param<uint32_t> p_pidr3;
    cci::cci_param<uint32_t> p_cidr0;
    cci::cci_param<uint32_t> p_cidr1;
    cci::cci_param<uint32_t> p_cidr2;
    cci::cci_param<uint32_t> p_cidr3;

    tlm_utils::simple_target_socket<zena_ssu, DEFAULT_TLM_BUSWIDTH> target_socket;
    TargetSignalSocket<bool> critical_in;
    TargetSignalSocket<bool> non_critical_in;
    InitiatorSignalSocket<bool> safety_status;

    explicit zena_ssu(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_enforce_sys_key("enforce_sys_key", true)
        , p_pidr4("pidr4", 0x00000004)
        , p_pidr0("pidr0", 0x000000d1)
        , p_pidr1("pidr1", 0x000000b3)
        , p_pidr2("pidr2", 0x0000000b)
        , p_pidr3("pidr3", 0x00000000)
        , p_cidr0("cidr0", 0x0000000d)
        , p_cidr1("cidr1", 0x000000f0)
        , p_cidr2("cidr2", 0x00000005)
        , p_cidr3("cidr3", 0x000000b1)
        , target_socket("target_socket")
        , critical_in("critical_in")
        , non_critical_in("non_critical_in")
        , safety_status("safety_status")
    {
        reset_registers();
        target_socket.register_b_transport(this, &zena_ssu::b_transport);
        target_socket.register_transport_dbg(this, &zena_ssu::transport_dbg);

        critical_in.register_value_changed_cb([this](bool value) {
            if (value) {
                set_fault(true);
            }
        });
        non_critical_in.register_value_changed_cb([this](bool value) {
            if (value) {
                set_fault(false);
            }
        });
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
