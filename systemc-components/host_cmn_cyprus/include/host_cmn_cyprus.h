/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <unordered_map>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class host_cmn_cyprus : public sc_core::sc_module
{
    static constexpr uint64_t DEFAULT_WINDOW_BYTES = 0x40000000;
    static constexpr uint64_t CFGM_BASE = 0x00000;
    static constexpr uint64_t MXP_BASE = 0x10000;
    static constexpr uint64_t NODE_BASE = 0x20000;
    static constexpr uint64_t NODE_STRIDE = 0x10000;

    static constexpr uint64_t CFGM_PERIPH_ID1 = 0x010;
    static constexpr uint64_t CFGM_CHILD_INFO = 0x080;
    static constexpr uint64_t CFGM_CHILD_POINTER = 0x100;

    static constexpr uint64_t MXP_PORT_CONNECT_INFO = 0x008;
    static constexpr uint64_t MXP_CHILD_INFO = 0x080;
    static constexpr uint64_t MXP_CHILD_POINTER = 0x100;
    static constexpr uint64_t MXP_PORT_DISABLE = 0xa70;

    static constexpr uint64_t NODE_CHILD_INFO = 0x080;
    static constexpr uint64_t HNS_UNIT_INFO0 = 0x900;
    static constexpr uint64_t HNS_UNIT_INFO1 = 0x908;
    static constexpr uint64_t RNSAM_UNIT_INFO0 = 0x900;
    static constexpr uint64_t RNSAM_UNIT_INFO1 = 0x908;
    static constexpr uint64_t RNSAM_STATUS = 0x1100;

    static constexpr uint16_t NODE_TYPE_CFG = 0x002;
    static constexpr uint16_t NODE_TYPE_XP = 0x006;
    static constexpr uint16_t NODE_TYPE_RN_SAM = 0x00f;
    static constexpr uint16_t NODE_TYPE_HN_S = 0x200;

    static constexpr uint8_t DEVICE_TYPE_HN_S = 0x1a;
    static constexpr uint8_t DEVICE_TYPE_RN_F_CHIF_ESAM = 0x21;

    static constexpr uint64_t RNSAM_NONHASH_RCOMP_EN = UINT64_C(1) << 31;
    static constexpr uint64_t RNSAM_HTG_RCOMP_EN = UINT64_C(1) << 27;
    static constexpr uint64_t RNSAM_RCOMP_LSB_DEFAULT = 20;

    static constexpr std::array<uint16_t, 8> HNS_NODE_IDS = {
        64, 88, 128, 152, 192, 216, 256, 280,
    };
    static constexpr uint16_t RNSAM_NODE_ID = 12;

    std::unordered_map<uint64_t, uint8_t> m_regs;
    unsigned int m_trace_count = 0;

    static bool is_supported_length(unsigned int len)
    {
        return len == 1 || len == 2 || len == 4 || len == 8;
    }

    static uint64_t node_info(uint16_t type, uint16_t node_id,
                              uint16_t logical_id)
    {
        return (static_cast<uint64_t>(logical_id) << 32) |
            (static_cast<uint64_t>(node_id) << 16) | type;
    }

    static uint64_t mxp_node_info(uint16_t node_id, uint16_t logical_id,
                                  uint8_t port_count)
    {
        return (static_cast<uint64_t>(port_count) << 48) |
            node_info(NODE_TYPE_XP, node_id, logical_id);
    }

    uint8_t load8(uint64_t offset) const
    {
        const auto it = m_regs.find(offset);
        return it == m_regs.end() ? 0 : it->second;
    }

    uint64_t load_le(uint64_t offset, unsigned int len) const
    {
        uint64_t value = 0;
        for (unsigned int i = 0; i < len; ++i)
            value |= static_cast<uint64_t>(load8(offset + i)) << (i * 8);
        return value;
    }

    void store_le(uint64_t offset, uint64_t value, unsigned int len)
    {
        for (unsigned int i = 0; i < len; ++i) {
            m_regs[offset + i] = static_cast<uint8_t>(value >> (i * 8));
        }
    }

    void seed64(uint64_t offset, uint64_t value)
    {
        store_le(offset, value, sizeof(value));
    }

    static uint64_t hns_offset(unsigned int index)
    {
        return NODE_BASE + (index * NODE_STRIDE);
    }

    static uint64_t rnsam_offset()
    {
        return NODE_BASE + (HNS_NODE_IDS.size() * NODE_STRIDE);
    }

    void seed_rnsam_node()
    {
        const uint64_t base = rnsam_offset();
        const uint64_t rcomp_lsb =
            static_cast<uint64_t>(p_rcomp_lsb.get_value());

        seed64(base, node_info(NODE_TYPE_RN_SAM, RNSAM_NODE_ID, 0));
        seed64(base + NODE_CHILD_INFO, 0);
        seed64(base + RNSAM_UNIT_INFO0,
               RNSAM_NONHASH_RCOMP_EN | RNSAM_HTG_RCOMP_EN);
        seed64(base + RNSAM_UNIT_INFO1, (rcomp_lsb << 5) | rcomp_lsb);
        seed64(base + RNSAM_STATUS, 0x2);
    }

    void seed_registers()
    {
        m_regs.clear();
        m_trace_count = 0;

        seed64(CFGM_BASE, node_info(NODE_TYPE_CFG, 0, 0));
        seed64(CFGM_PERIPH_ID1,
               static_cast<uint64_t>(p_revision.get_value() & 0xf) << 4);
        seed64(CFGM_CHILD_INFO, 1);
        seed64(CFGM_CHILD_POINTER, MXP_BASE);

        seed64(MXP_BASE, mxp_node_info(0, 0, 4));
        seed64(MXP_BASE + MXP_PORT_CONNECT_INFO + (0 * sizeof(uint64_t)),
               DEVICE_TYPE_HN_S);
        seed64(MXP_BASE + MXP_PORT_CONNECT_INFO + (2 * sizeof(uint64_t)),
               DEVICE_TYPE_RN_F_CHIF_ESAM);
        seed64(MXP_BASE + MXP_CHILD_INFO, HNS_NODE_IDS.size() + 1);
        seed64(MXP_BASE + MXP_PORT_DISABLE, 0);

        for (unsigned int i = 0; i < HNS_NODE_IDS.size(); ++i) {
            const uint64_t base = hns_offset(i);
            seed64(MXP_BASE + MXP_CHILD_POINTER + (i * sizeof(uint64_t)),
                   base);
            seed64(base, node_info(NODE_TYPE_HN_S, HNS_NODE_IDS[i], i));
            seed64(base + NODE_CHILD_INFO, 0);
            seed64(base + HNS_UNIT_INFO0, 0);
            seed64(base + HNS_UNIT_INFO1, 0);
        }

        seed64(MXP_BASE + MXP_CHILD_POINTER +
                   (HNS_NODE_IDS.size() * sizeof(uint64_t)),
               rnsam_offset());
        seed_rnsam_node();
    }

    bool is_in_window(uint64_t offset, unsigned int len) const
    {
        const uint64_t window = p_window_bytes.get_value();
        return offset < window && len <= (window - offset);
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) ||
            !is_in_window(offset, len)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            const uint64_t value = load_le(offset, len);
            std::memcpy(data, &value, len);
        } else if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            uint64_t value = 0;
            std::memcpy(&value, data, len);
            store_le(offset, value, len);
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
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value())
            return;

        ++m_trace_count;
        uint64_t value = 0;
        if (len <= sizeof(value))
            std::memcpy(&value, trans.get_data_ptr(), len);

        std::cerr << name() << " "
                  << (debug ? "dbg_" : "")
                  << (trans.get_command() == tlm::TLM_READ_COMMAND ? "read" :
                                                                      "write")
                  << " offset=0x" << std::hex << offset
                  << " len=0x" << len
                  << " value=0x" << value
                  << std::dec << std::endl;
    }

public:
    cci::cci_param<uint32_t> p_revision;
    cci::cci_param<uint32_t> p_rcomp_lsb;
    cci::cci_param<uint64_t> p_window_bytes;
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    tlm_utils::simple_target_socket<host_cmn_cyprus, DEFAULT_TLM_BUSWIDTH>
        target_socket;

    explicit host_cmn_cyprus(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_revision("revision", 0)
        , p_rcomp_lsb("rcomp_lsb", RNSAM_RCOMP_LSB_DEFAULT)
        , p_window_bytes("window_bytes", DEFAULT_WINDOW_BYTES)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 128)
        , target_socket("target_socket")
    {
        seed_registers();
        target_socket.register_b_transport(this, &host_cmn_cyprus::b_transport);
        target_socket.register_transport_dbg(this,
                                             &host_cmn_cyprus::transport_dbg);
    }

    void before_end_of_elaboration() override
    {
        seed_registers();
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
