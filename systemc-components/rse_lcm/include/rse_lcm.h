/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class rse_lcm : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x11000;
    static constexpr uint32_t LCS_VALUE = 0x000;
    static constexpr uint32_t KEY_ERR = 0x004;
    static constexpr uint32_t TP_MODE = 0x008;
    static constexpr uint32_t FATAL_ERR = 0x00c;
    static constexpr uint32_t SP_ENABLE = 0x014;
    static constexpr uint32_t OTP_ADDR_WIDTH = 0x018;
    static constexpr uint32_t OTP_SIZE = 0x01c;
    static constexpr uint32_t GPPC = 0x020;
    static constexpr uint32_t DCU_EN = 0x100;
    static constexpr uint32_t DCU_LOCK = 0x110;
    static constexpr uint32_t OTP_WINDOW = 0x1000;

    std::array<uint8_t, REG_BYTES> m_regs{};
    bool m_otp_locked = false;
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

    uint64_t otp_limit() const
    {
        const uint64_t configured_size = p_otp_size.get_value();
        const uint64_t max_size = REG_BYTES - OTP_WINDOW;
        return OTP_WINDOW + std::min(configured_size, max_size);
    }

    bool is_otp_access(uint64_t offset, unsigned int len) const
    {
        return offset >= OTP_WINDOW && offset + len <= otp_limit();
    }

    void flush_otp_image()
    {
        if (!p_otp_writeback.get_value()) {
            return;
        }

        const std::string path = p_otp_image.get_value();
        if (path.empty()) {
            return;
        }

        const uint64_t bytes = otp_limit() - OTP_WINDOW;
        const std::streamsize stream_bytes = static_cast<std::streamsize>(bytes);
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file) {
            std::ofstream create(path, std::ios::binary);
            if (!create) {
                std::cerr << name() << " unable to create otp_image=" << path << std::endl;
                return;
            }
            create.write(reinterpret_cast<const char*>(&m_regs[OTP_WINDOW]), stream_bytes);
            if (!create) {
                std::cerr << name() << " unable to write otp_image=" << path << std::endl;
            }
            return;
        }

        file.seekp(0, std::ios::beg);
        if (!file) {
            std::cerr << name() << " unable to seek otp_image=" << path << std::endl;
            return;
        }

        file.write(reinterpret_cast<const char*>(&m_regs[OTP_WINDOW]), stream_bytes);
        if (!file) {
            std::cerr << name() << " unable to write otp_image=" << path << std::endl;
        }
    }

    bool write_otp(uint64_t offset, const uint8_t* data, unsigned int len)
    {
        if (!is_otp_access(offset, len)) {
            return false;
        }

        if (m_otp_locked) {
            return true;
        }

        std::memcpy(&m_regs[offset], data, len);
        flush_otp_image();
        return true;
    }

    void load_otp_image()
    {
        const std::string path = p_otp_image.get_value();
        if (path.empty()) {
            return;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << name() << " unable to open otp_image=" << path << std::endl;
            return;
        }

        file.read(reinterpret_cast<char*>(&m_regs[OTP_WINDOW]), REG_BYTES - OTP_WINDOW);
    }

    void reset_registers()
    {
        m_regs.fill(0);
        m_otp_locked = false;

        store32(LCS_VALUE, p_lcs.get_value());
        store32(KEY_ERR, 0x00000000);
        store32(TP_MODE, p_tp_mode.get_value());
        store32(FATAL_ERR, 0x00000000);
        store32(SP_ENABLE, p_sp_enable.get_value());
        store32(OTP_ADDR_WIDTH, 0x00000010);
        store32(OTP_SIZE, p_otp_size.get_value());
        store32(GPPC, p_gppc.get_value());
        for (uint32_t i = 0; i < 4; ++i) {
            store32(DCU_EN + i * sizeof(uint32_t), 0xffffffff);
            store32(DCU_LOCK + i * sizeof(uint32_t), 0x00000000);
        }

        load_otp_image();
    }

    void write32(uint32_t offset, uint32_t value)
    {
        switch (offset) {
        case LCS_VALUE:
        case KEY_ERR:
        case TP_MODE:
        case OTP_ADDR_WIDTH:
        case OTP_SIZE:
        case GPPC:
            break;
        case SP_ENABLE:
            if (value == 0x5ec10e1e) {
                store32(SP_ENABLE, 0xffffffff);
                if (p_otp_lock_after_provision.get_value()) {
                    m_otp_locked = true;
                }
            } else {
                store32(SP_ENABLE, value);
            }
            break;
        case FATAL_ERR:
        case DCU_EN:
        case DCU_EN + 4:
        case DCU_EN + 8:
        case DCU_EN + 12:
        case DCU_LOCK:
        case DCU_LOCK + 4:
        case DCU_LOCK + 8:
        case DCU_LOCK + 12:
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
            if (write_otp(offset, data, len)) {
                /* handled by OTP window semantics */
            } else if (len == sizeof(uint32_t) && (offset % sizeof(uint32_t)) == 0) {
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
    cci::cci_param<std::string> p_otp_image;
    cci::cci_param<uint32_t> p_lcs;
    cci::cci_param<uint32_t> p_tp_mode;
    cci::cci_param<uint32_t> p_sp_enable;
    cci::cci_param<uint32_t> p_otp_size;
    cci::cci_param<uint32_t> p_gppc;
    cci::cci_param<bool> p_otp_writeback;
    cci::cci_param<bool> p_otp_lock_after_provision;
    tlm_utils::simple_target_socket<rse_lcm, DEFAULT_TLM_BUSWIDTH> target_socket;

    explicit rse_lcm(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_otp_image("otp_image", "")
        , p_lcs("lcs", 0xeeeea5a5)
        , p_tp_mode("tp_mode", 0x2222aa55)
        , p_sp_enable("sp_enable", 0x00000000)
        , p_otp_size("otp_size", 0x00010000)
        , p_gppc("gppc", 0x00000000)
        , p_otp_writeback("otp_writeback", false)
        , p_otp_lock_after_provision("otp_lock_after_provision", true)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this, &rse_lcm::b_transport);
        target_socket.register_transport_dbg(this, &rse_lcm::transport_dbg);
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
