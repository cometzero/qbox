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

class host_scr : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x10000;
    static constexpr uint32_t SID_SYSTEM_ID = 0x040;
    static constexpr uint32_t SID_SOC_ID = 0x050;
    static constexpr uint32_t SID_CHIP_ID = 0x060;
    static constexpr uint32_t SID_SYSTEM_CFG = 0x070;
    static constexpr uint32_t CPUHALT = 0x300;
    static constexpr uint32_t MEMPROTCTLR = 0x500;
    static constexpr uint32_t SAFECTLR = 0x600;
    static constexpr uint32_t SID_PIDR4 = 0xfd0;
    static constexpr uint32_t SID_PIDR0 = 0xfe0;
    static constexpr uint32_t SID_PIDR1 = 0xfe4;
    static constexpr uint32_t SID_PIDR2 = 0xfe8;
    static constexpr uint32_t SID_PIDR3 = 0xfec;
    static constexpr uint32_t SID_CIDR0 = 0xff0;
    static constexpr uint32_t SID_CIDR1 = 0xff4;
    static constexpr uint32_t SID_CIDR2 = 0xff8;
    static constexpr uint32_t SID_CIDR3 = 0xffc;

    std::array<uint8_t, REG_BYTES> m_regs {};
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

    uint32_t system_cfg_value() const
    {
        uint32_t value = p_cl1_present.get_value() ? 0x1u : 0x0u;
        value |= (p_lm0_size.get_value() & 0xfu) << 4;
        value |= (p_lm1_size.get_value() & 0xfu) << 8;
        return value;
    }

    void reset_registers()
    {
        m_regs.fill(0);
        store32(SID_SYSTEM_ID, p_system_id.get_value());
        store32(SID_SOC_ID, p_soc_id.get_value());
        store32(SID_CHIP_ID, p_chip_id.get_value());
        store32(SID_SYSTEM_CFG, system_cfg_value());
        store32(CPUHALT, p_initial_cpuhalt.get_value());
        store32(MEMPROTCTLR, p_initial_memprotctlr.get_value());
        store32(SAFECTLR, p_initial_safectlr.get_value());

        store32(SID_PIDR4, 0x00000004);
        store32(SID_PIDR0, 0x00000000);
        store32(SID_PIDR1, 0x00000000);
        store32(SID_PIDR2, 0x00000000);
        store32(SID_PIDR3, 0x00000000);
        store32(SID_CIDR0, 0x0000000d);
        store32(SID_CIDR1, 0x000000f0);
        store32(SID_CIDR2, 0x00000005);
        store32(SID_CIDR3, 0x000000b1);
    }

    void write32(uint32_t offset, uint32_t value)
    {
        switch (offset) {
        case SID_SYSTEM_ID:
        case SID_SOC_ID:
        case SID_CHIP_ID:
        case SID_SYSTEM_CFG:
        case SID_PIDR4:
        case SID_PIDR0:
        case SID_PIDR1:
        case SID_PIDR2:
        case SID_PIDR3:
        case SID_CIDR0:
        case SID_CIDR1:
        case SID_CIDR2:
        case SID_CIDR3:
            break;
        default:
            store32(offset, value);
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
    cci::cci_param<bool> p_cl1_present;
    cci::cci_param<uint32_t> p_lm0_size;
    cci::cci_param<uint32_t> p_lm1_size;
    cci::cci_param<uint32_t> p_system_id;
    cci::cci_param<uint32_t> p_soc_id;
    cci::cci_param<uint32_t> p_chip_id;
    cci::cci_param<uint32_t> p_initial_cpuhalt;
    cci::cci_param<uint32_t> p_initial_memprotctlr;
    cci::cci_param<uint32_t> p_initial_safectlr;
    tlm_utils::simple_target_socket<host_scr, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit host_scr(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_cl1_present("cl1_present", false)
        , p_lm0_size("lm0_size", 0)
        , p_lm1_size("lm1_size", 0)
        , p_system_id("system_id", 0)
        , p_soc_id("soc_id", 0)
        , p_chip_id("chip_id", 0)
        , p_initial_cpuhalt("initial_cpuhalt", 0)
        , p_initial_memprotctlr("initial_memprotctlr", 0)
        , p_initial_safectlr("initial_safectlr", 0)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &host_scr::b_transport);
        target_socket.register_transport_dbg(this, &host_scr::transport_dbg);
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
