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

class rse_sysctrl : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x1000;

    static constexpr uint32_t SECDBGSTAT = 0x000;
    static constexpr uint32_t SECDBGSET = 0x004;
    static constexpr uint32_t SECDBGCLR = 0x008;
    static constexpr uint32_t RESET_SYNDROME = 0x100;
    static constexpr uint32_t RESET_MASK = 0x104;
    static constexpr uint32_t SWRESET = 0x108;
    static constexpr uint32_t GRETREG = 0x10c;
    static constexpr uint32_t INITSVTOR0 = 0x110;
    static constexpr uint32_t CPUWAIT = 0x120;
    static constexpr uint32_t NMI_ENABLE = 0x124;
    static constexpr uint32_t PWRCTRL = 0x1fc;
    static constexpr uint32_t GRETEXREG = 0x250;
    static constexpr uint32_t DMA_BOOT_EN = 0x254;
    static constexpr uint32_t DMA_BOOT_ADDR = 0x258;
    static constexpr uint32_t LCM_DCU_FORCE_DIS = 0x25c;

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

        store32(RESET_SYNDROME, p_reset_syndrome.get_value());
        store32(CPUWAIT, p_cpuwait.get_value());
        store32(DMA_BOOT_EN, p_dma_boot_en.get_value());
        store32(DMA_BOOT_ADDR, p_dma_boot_addr.get_value());
    }

    void write32(uint32_t offset, uint32_t value)
    {
        switch (offset) {
        case SECDBGSET:
            store32(SECDBGSTAT, load32(SECDBGSTAT) | value);
            store32(SECDBGSET, value);
            break;
        case SECDBGCLR:
            store32(SECDBGSTAT, load32(SECDBGSTAT) & ~value);
            store32(SECDBGCLR, value);
            break;
        case SWRESET:
            store32(SWRESET, 0x00000000);
            break;
        case RESET_SYNDROME:
        case RESET_MASK:
        case GRETREG:
        case INITSVTOR0:
        case CPUWAIT:
        case NMI_ENABLE:
        case PWRCTRL:
        case GRETEXREG:
        case DMA_BOOT_EN:
        case DMA_BOOT_ADDR:
        case LCM_DCU_FORCE_DIS:
            store32(offset, value);
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
    cci::cci_param<uint32_t> p_reset_syndrome;
    cci::cci_param<uint32_t> p_cpuwait;
    cci::cci_param<uint32_t> p_dma_boot_en;
    cci::cci_param<uint32_t> p_dma_boot_addr;
    tlm_utils::simple_target_socket<rse_sysctrl, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit rse_sysctrl(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_reset_syndrome("reset_syndrome", 0x80000000)
        , p_cpuwait("cpuwait", 0x0000000f)
        , p_dma_boot_en("dma_boot_en", 0x00000001)
        , p_dma_boot_addr("dma_boot_addr", 0x00000000)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &rse_sysctrl::b_transport);
        target_socket.register_transport_dbg(this, &rse_sysctrl::transport_dbg);
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
