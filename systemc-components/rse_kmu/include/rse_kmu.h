/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
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
#include <ports/target-signal-socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class rse_kmu : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x1000;
    static constexpr uint32_t KMUBC = 0x000;
    static constexpr uint32_t KMUIS = 0x004;
    static constexpr uint32_t KMUIE = 0x008;
    static constexpr uint32_t KMUIC = 0x00c;
    static constexpr uint32_t KMUPRBGSI = 0x010;
    static constexpr uint32_t KMUKSC_BASE = 0x030;
    static constexpr uint32_t KMUDKPA_BASE = 0x0b0;
    static constexpr uint32_t KMUKSK_BASE = 0x130;
    static constexpr uint32_t KMURD_8 = 0x530;
    static constexpr uint32_t KMURD_16 = 0x534;
    static constexpr uint32_t KMURD_32 = 0x538;
    static constexpr uint32_t PIDR4 = 0xfd0;
    static constexpr uint32_t PIDR0 = 0xfe0;
    static constexpr uint32_t PIDR1 = 0xfe4;
    static constexpr uint32_t PIDR2 = 0xfe8;
    static constexpr uint32_t PIDR3 = 0xfec;
    static constexpr uint32_t CIDR0 = 0xff0;
    static constexpr uint32_t CIDR1 = 0xff4;
    static constexpr uint32_t CIDR2 = 0xff8;
    static constexpr uint32_t CIDR3 = 0xffc;

    static constexpr uint32_t SLOT_COUNT = 32;
    static constexpr uint32_t HW_SLOT_COUNT = 7;
    static constexpr uint32_t SLOT_WORDS = 8;
    static constexpr uint32_t WORD_BYTES = sizeof(uint32_t);
    static constexpr uint32_t OTP_HW_KEY_BYTES = SLOT_WORDS * WORD_BYTES;
    static constexpr uint32_t KMUKSC_STRIDE = WORD_BYTES;
    static constexpr uint32_t KMUDKPA_STRIDE = WORD_BYTES;
    static constexpr uint32_t KMUKSK_STRIDE = SLOT_WORDS * WORD_BYTES;

    static constexpr uint32_t KMUIS_KEC = 1u << 0;
    static constexpr uint32_t KMUKSC_LKS = 1u << 22;
    static constexpr uint32_t KMUKSC_LKSKR = 1u << 23;
    static constexpr uint32_t KMUKSC_VKS = 1u << 24;
    static constexpr uint32_t KMUKSC_KSR = 1u << 25;
    static constexpr uint32_t KMUKSC_IKS = 1u << 26;
    static constexpr uint32_t KMUKSC_KSIP = 1u << 27;
    static constexpr uint32_t KMUKSC_EK = 1u << 28;

    std::array<uint8_t, REG_BYTES> m_regs{};
    unsigned int m_trace_count = 0;

    using initiator_socket_type = tlm_utils::simple_initiator_socket_b<
        rse_kmu, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types, sc_core::SC_ZERO_OR_MORE_BOUND>;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    static bool is_slot_reg(uint32_t offset, uint32_t base)
    {
        return offset >= base &&
               offset < base + (SLOT_COUNT * WORD_BYTES) &&
               ((offset - base) % WORD_BYTES) == 0;
    }

    static bool is_key_reg(uint32_t offset)
    {
        return offset >= KMUKSK_BASE &&
               offset < KMUKSK_BASE + (SLOT_COUNT * KMUKSK_STRIDE);
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

        store32(KMUBC, p_build_config.get_value());
        store32(KMUIS, 0x00000000);
        store32(KMUIE, 0x00000000);
        store32(KMUIC, 0x00000000);
        store32(KMUPRBGSI, 0x00000000);

        for (uint32_t slot = 0; slot < HW_SLOT_COUNT; ++slot) {
            store32(KMUKSC_BASE + slot * KMUKSC_STRIDE, p_hw_slot_config.get_value());
            store32(KMUDKPA_BASE + slot * KMUDKPA_STRIDE, p_hw_slot_export_address.get_value());
        }
        load_otp_hardware_keys();

        store32(KMURD_8, 0x00000000);
        store32(KMURD_16, 0x00000000);
        store32(KMURD_32, 0x00000000);
        store32(PIDR4, 0x00000004);
        store32(PIDR0, 0x000000f3);
        store32(PIDR1, 0x000000b0);
        store32(PIDR2, 0x0000001b);
        store32(PIDR3, 0x00000000);
        store32(CIDR0, 0x0000000d);
        store32(CIDR1, 0x000000f0);
        store32(CIDR2, 0x00000005);
        store32(CIDR3, 0x000000b1);
    }

    void load_otp_hardware_keys()
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

        const std::array<uint32_t, HW_SLOT_COUNT> otp_offsets = {{
            0xffffffffu, // KRTL is not stored in the LCM OTP hardware area.
            0x00000000u, // HUK
            0x00000020u, // GUK
            0x00000040u, // KP_CM
            0x00000060u, // KCE_CM
            0x00000080u, // KP_DM
            0x000000a0u, // KCE_DM
        }};

        for (uint32_t slot = 1; slot < HW_SLOT_COUNT; ++slot) {
            file.seekg(otp_offsets[slot], std::ios::beg);
            if (!file) {
                std::cerr << name() << " unable to seek otp_image=" << path
                          << " offset=0x" << std::hex << otp_offsets[slot]
                          << std::dec << std::endl;
                return;
            }

            file.read(reinterpret_cast<char*>(&m_regs[KMUKSK_BASE + slot * KMUKSK_STRIDE]),
                      OTP_HW_KEY_BYTES);
            if (file.gcount() != OTP_HW_KEY_BYTES) {
                std::cerr << name() << " short otp_image=" << path
                          << " offset=0x" << std::hex << otp_offsets[slot]
                          << std::dec << std::endl;
                return;
            }
        }
    }

    bool mem_write32(uint64_t address, uint32_t value, sc_core::sc_time& delay)
    {
        if (initiator_socket.size() == 0) {
            return false;
        }

        tlm::tlm_generic_payload trans;

        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(address);
        trans.set_data_ptr(reinterpret_cast<uint8_t*>(&value));
        trans.set_data_length(sizeof(value));
        trans.set_streaming_width(sizeof(value));
        trans.set_byte_enable_ptr(nullptr);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);

        initiator_socket->b_transport(trans, delay);
        return trans.is_response_ok();
    }

    void export_key(uint32_t slot, sc_core::sc_time& delay)
    {
        uint64_t address = load32(KMUDKPA_BASE + slot * KMUDKPA_STRIDE);

        if (address == 0) {
            address = p_hw_slot_export_address.get_value();
        }

        for (uint32_t word = 0; word < SLOT_WORDS; ++word) {
            const uint32_t key_word =
                load32(KMUKSK_BASE + slot * KMUKSK_STRIDE + word * WORD_BYTES);
            (void)mem_write32(address + word * WORD_BYTES, key_word, delay);
        }
    }

    void write_kmuksc(uint32_t offset, uint32_t value, sc_core::sc_time& delay)
    {
        const uint32_t slot = (offset - KMUKSC_BASE) / KMUKSC_STRIDE;

        if (value & KMUKSC_IKS) {
            value &= ~KMUKSC_IKS;
            value |= KMUKSC_KSIP;
        }

        if (value & KMUKSC_VKS) {
            value &= ~KMUKSC_VKS;
            value |= KMUKSC_KSR;
        }

        if (value & KMUKSC_EK) {
            export_key(slot, delay);
            value &= ~KMUKSC_EK;
            value |= KMUKSC_KSR;
            store32(KMUIS, load32(KMUIS) | KMUIS_KEC);
        }

        store32(offset, value);
    }

    void write32(uint32_t offset, uint32_t value, sc_core::sc_time& delay)
    {
        switch (offset) {
        case KMUBC:
        case KMUIS:
        case KMURD_8:
        case KMURD_16:
        case KMURD_32:
        case PIDR4:
        case PIDR0:
        case PIDR1:
        case PIDR2:
        case PIDR3:
        case CIDR0:
        case CIDR1:
        case CIDR2:
        case CIDR3:
            break;
        case KMUIC:
            store32(KMUIS, load32(KMUIS) & ~value);
            store32(KMUIC, value);
            break;
        case KMUIE:
        case KMUPRBGSI:
            store32(offset, value);
            break;
        default:
            if (is_slot_reg(offset, KMUKSC_BASE)) {
                write_kmuksc(offset, value, delay);
            } else {
                store32(offset, value);
            }
            break;
        }
    }

    bool access(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay, bool debug)
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
                write32(static_cast<uint32_t>(offset), value, delay);
            } else if (is_key_reg(static_cast<uint32_t>(offset))) {
                std::memcpy(&m_regs[offset], data, len);
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
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value() ||
            !trace_filter_matches(offset)) {
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

    bool trace_filter_matches(uint64_t offset) const
    {
        const auto filter = p_trace_filter.get_value();

        if (filter.empty() || filter == "all") {
            return true;
        }

        if (filter == "key") {
            return offset == KMUIS ||
                   offset == KMUIC ||
                   is_slot_reg(static_cast<uint32_t>(offset), KMUKSC_BASE) ||
                   is_slot_reg(static_cast<uint32_t>(offset), KMUDKPA_BASE) ||
                   is_key_reg(static_cast<uint32_t>(offset));
        }

        return false;
    }

public:
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<std::string> p_trace_filter;
    cci::cci_param<std::string> p_otp_image;
    cci::cci_param<uint32_t> p_build_config;
    cci::cci_param<uint32_t> p_hw_slot_config;
    cci::cci_param<uint32_t> p_hw_slot_export_address;
    initiator_socket_type initiator_socket;
    tlm_utils::simple_target_socket<rse_kmu, DEFAULT_TLM_BUSWIDTH> target_socket;
    TargetSignalSocket<bool> reset;

    explicit rse_kmu(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , p_trace_filter("trace_filter", "all")
        , p_otp_image("otp_image", "")
        , p_build_config("build_config", 0x003d0005)
        , p_hw_slot_config("hw_slot_config", 0x00d60100)
        , p_hw_slot_export_address("hw_slot_export_address", 0x50154400)
        , initiator_socket("initiator_socket")
        , target_socket("target_socket")
        , reset("reset")
    {
        reset_registers();
        target_socket.register_b_transport(this, &rse_kmu::b_transport);
        target_socket.register_transport_dbg(this, &rse_kmu::transport_dbg);
        reset.register_value_changed_cb([this](bool value) { doreset(value); });
    }

    void doreset(bool value)
    {
        if (value) {
            reset_registers();
        }
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        trans.set_dmi_allowed(false);
        access(trans, delay, false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        return access(trans, delay, true) ? trans.get_data_length() : 0;
    }
};

extern "C" void module_register();
