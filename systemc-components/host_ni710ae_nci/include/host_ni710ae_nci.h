/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <systemc>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_target_socket.h>

class host_ni710ae_nci : public sc_core::sc_module
{
    static constexpr uint64_t REG_BYTES = 0x10000;
    static constexpr uint32_t ROOT_OFFSET = 0x0000;
    static constexpr uint32_t COMPONENT_BASE = 0x0100;
    static constexpr uint32_t COMPONENT_STRIDE = 0x0100;
    static constexpr uint32_t APU_BASE = 0x2000;
    static constexpr uint32_t APU_STRIDE = 0x1000;

    static constexpr uint16_t NODE_TYPE_GCN = 0x00;
    static constexpr uint16_t NODE_TYPE_ASNI = 0x04;
    static constexpr uint16_t NODE_TYPE_AMNI = 0x05;
    static constexpr uint16_t NODE_TYPE_SUBFEATURE_APU = 0x00;

    static constexpr uint32_t DOMAIN_CHILD_INFO = 0x004;
    static constexpr uint32_t DOMAIN_POINTERS = 0x008;
    static constexpr uint32_t COMPONENT_NUM_SUBFEATURES = 0x024;
    static constexpr uint32_t COMPONENT_SUBFEATURES = 0x028;
    static constexpr uint32_t SUBFEATURE_ENTRY_BYTES = 0x008;

    static constexpr uint32_t APU_CTLR = 0x0ff8;
    static constexpr uint32_t APU_IIDR = 0x0ffc;

    enum topology_t : uint32_t {
        TOPOLOGY_MHU_MIN = 0,
        TOPOLOGY_MHU_MID = 1,
        TOPOLOGY_SECONDARY = 2,
        TOPOLOGY_PRIMARY_MIN = 3,
        TOPOLOGY_PRIMARY_MID = 4,
    };

    struct component_desc {
        uint16_t type;
        uint16_t id;
    };

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

    static uint32_t node_type_word(uint16_t type, uint16_t id)
    {
        return (static_cast<uint32_t>(id) << 16) | type;
    }

    static std::vector<component_desc> components_for_topology(uint32_t topology)
    {
        switch (topology) {
        case TOPOLOGY_MHU_MIN:
            return { { NODE_TYPE_ASNI, 3 } };
        case TOPOLOGY_MHU_MID:
            return { { NODE_TYPE_ASNI, 4 } };
        case TOPOLOGY_SECONDARY:
            return { { NODE_TYPE_ASNI, 1 } };
        case TOPOLOGY_PRIMARY_MIN:
            return {
                { NODE_TYPE_ASNI, 0 },
                { NODE_TYPE_ASNI, 3 },
                { NODE_TYPE_ASNI, 4 },
                { NODE_TYPE_ASNI, 5 },
                { NODE_TYPE_AMNI, 10 },
            };
        case TOPOLOGY_PRIMARY_MID:
        default:
            return {
                { NODE_TYPE_ASNI, 0 },
                { NODE_TYPE_ASNI, 5 },
                { NODE_TYPE_ASNI, 6 },
                { NODE_TYPE_ASNI, 7 },
                { NODE_TYPE_AMNI, 14 },
            };
        }
    }

    void seed_discovery_component(uint32_t index, const component_desc& desc)
    {
        const uint32_t component_offset =
            COMPONENT_BASE + (index * COMPONENT_STRIDE);
        const uint32_t apu_offset = APU_BASE + (index * APU_STRIDE);

        store32(DOMAIN_POINTERS + (index * sizeof(uint32_t)),
                component_offset);
        store32(component_offset, node_type_word(desc.type, desc.id));
        store32(component_offset + COMPONENT_NUM_SUBFEATURES, 1);
        store32(component_offset + COMPONENT_SUBFEATURES,
                NODE_TYPE_SUBFEATURE_APU);
        store32(component_offset + COMPONENT_SUBFEATURES + sizeof(uint32_t),
                apu_offset);
        store32(apu_offset + APU_IIDR, p_apu_iidr.get_value());
    }

    void reset_registers()
    {
        m_regs.fill(0);
        const auto components = components_for_topology(p_topology.get_value());
        store32(ROOT_OFFSET, node_type_word(NODE_TYPE_GCN, 0));
        store32(DOMAIN_CHILD_INFO, static_cast<uint32_t>(components.size()));
        for (uint32_t i = 0; i < components.size(); ++i) {
            seed_discovery_component(i, components[i]);
        }
    }

    bool is_apu_iidr(uint32_t offset) const
    {
        if (offset < APU_BASE)
            return false;
        const uint32_t apu_relative = offset - APU_BASE;
        return (apu_relative % APU_STRIDE) == APU_IIDR;
    }

    void write32(uint32_t offset, uint32_t value)
    {
        if (is_apu_iidr(offset))
            return;
        store32(offset, value);
    }

    bool access(tlm::tlm_generic_payload& trans, bool debug)
    {
        const uint64_t offset = trans.get_address();
        const unsigned int len = trans.get_data_length();
        uint8_t* data = trans.get_data_ptr();

        if (data == nullptr || !is_supported_length(len) ||
            offset + len > m_regs.size()) {
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

    void trace_access(tlm::tlm_generic_payload& trans, uint64_t offset,
                      unsigned int len, bool debug)
    {
        if (!p_trace.get_value() || m_trace_count >= p_trace_limit.get_value())
            return;

        ++m_trace_count;
        uint32_t value = 0;
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
    cci::cci_param<uint32_t> p_topology;
    cci::cci_param<uint32_t> p_apu_iidr;
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    tlm_utils::simple_target_socket<host_ni710ae_nci, DEFAULT_TLM_BUSWIDTH>
        target_socket;

    explicit host_ni710ae_nci(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_topology("topology", TOPOLOGY_PRIMARY_MID)
        , p_apu_iidr("apu_iidr", 0)
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 64)
        , target_socket("target_socket")
    {
        reset_registers();
        target_socket.register_b_transport(this,
                                           &host_ni710ae_nci::b_transport);
        target_socket.register_transport_dbg(this,
                                             &host_ni710ae_nci::transport_dbg);
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
