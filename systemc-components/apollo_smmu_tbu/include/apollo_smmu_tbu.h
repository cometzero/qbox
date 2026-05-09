/*
 * Apollo functional SMMU/TBU bridge.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <cci_configuration>
#include <module_factory_registery.h>
#include <scp/report.h>
#include <tlm>
#include <tlm_sockets_buswidth.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

class apollo_smmu_tbu : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    tlm_utils::simple_target_socket<apollo_smmu_tbu, DEFAULT_TLM_BUSWIDTH> upstream;
    tlm_utils::simple_target_socket<apollo_smmu_tbu, DEFAULT_TLM_BUSWIDTH> regs;
    tlm_utils::simple_initiator_socket<apollo_smmu_tbu, DEFAULT_TLM_BUSWIDTH> downstream;

    cci::cci_param<uint32_t> p_stream_id;
    cci::cci_param<uint64_t> p_iova_base;
    cci::cci_param<uint64_t> p_pa_base;
    cci::cci_param<uint64_t> p_window_size;

    explicit apollo_smmu_tbu(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , upstream("upstream")
        , regs("regs")
        , downstream("downstream")
        , p_stream_id("stream_id", 1, "StreamID associated with this TBU")
        , p_iova_base("iova_base", 0x10000000ULL, "Base IOVA accepted by this TBU")
        , p_pa_base("pa_base", 0x00a00000ULL, "Translated physical base for the IOVA window")
        , p_window_size("window_size", 0x00600000ULL, "Size of translated IOVA window")
    {
        upstream.register_b_transport(this, &apollo_smmu_tbu::b_transport);
        upstream.register_transport_dbg(this, &apollo_smmu_tbu::transport_dbg);
        regs.register_b_transport(this, &apollo_smmu_tbu::regs_b_transport);
        regs.register_transport_dbg(this, &apollo_smmu_tbu::regs_transport_dbg);
    }

private:
    enum : uint64_t {
        REG_MAP_IOVA_LO = 0x00,
        REG_MAP_IOVA_HI = 0x04,
        REG_MAP_PA_LO = 0x08,
        REG_MAP_PA_HI = 0x0c,
        REG_MAP_SIZE_LO = 0x10,
        REG_MAP_SIZE_HI = 0x14,
        REG_MAP_CTRL = 0x18,
        REG_MAP_STATUS = 0x1c,
        REG_MAP_COUNT = 0x20,
    };

    enum : uint32_t {
        MAP_CTRL_ADD = 1,
        MAP_CTRL_REMOVE = 2,
        MAP_STATUS_IDLE = 0,
        MAP_STATUS_OK = 1,
        MAP_STATUS_ERROR = 2,
    };

    struct map_entry {
        uint64_t iova_base = 0;
        uint64_t pa_base = 0;
        uint64_t size = 0;
        bool valid = false;
    };

    std::array<map_entry, 8> m_maps {};
    uint64_t m_map_iova = 0;
    uint64_t m_map_pa = 0;
    uint64_t m_map_size = 0;
    uint32_t m_map_status = MAP_STATUS_IDLE;
    bool m_dynamic_enabled = false;

    bool translate(uint64_t iova, uint64_t len, uint64_t& pa) const
    {
        if (m_dynamic_enabled) {
            for (const auto& map : m_maps) {
                if (!map.valid || len == 0 || iova < map.iova_base) {
                    continue;
                }

                const uint64_t offset = iova - map.iova_base;
                if (offset < map.size && len <= map.size - offset) {
                    pa = map.pa_base + offset;
                    return true;
                }
            }

            return false;
        }

        const uint64_t base = p_iova_base.get_value();
        const uint64_t size = p_window_size.get_value();

        if (len == 0 || size == 0 || iova < base) {
            return false;
        }

        const uint64_t offset = iova - base;
        if (offset >= size || len > size - offset) {
            return false;
        }

        pa = p_pa_base.get_value() + offset;
        return true;
    }

    uint32_t map_count() const
    {
        uint32_t count = 0;

        for (const auto& map : m_maps) {
            if (map.valid) {
                count++;
            }
        }

        return count;
    }

    void add_map()
    {
        if (m_map_size == 0) {
            m_map_status = MAP_STATUS_ERROR;
            log_map_error("map", m_map_iova, m_map_pa, m_map_size);
            return;
        }

        m_dynamic_enabled = true;
        for (auto& map : m_maps) {
            if (map.valid && map.iova_base == m_map_iova) {
                map.pa_base = m_map_pa;
                map.size = m_map_size;
                m_map_status = MAP_STATUS_OK;
                log_map("remap", m_map_iova, m_map_pa, m_map_size);
                return;
            }
        }

        for (auto& map : m_maps) {
            if (!map.valid) {
                map.iova_base = m_map_iova;
                map.pa_base = m_map_pa;
                map.size = m_map_size;
                map.valid = true;
                m_map_status = MAP_STATUS_OK;
                log_map("map", m_map_iova, m_map_pa, m_map_size);
                return;
            }
        }

        m_map_status = MAP_STATUS_ERROR;
        log_map_error("map-full", m_map_iova, m_map_pa, m_map_size);
    }

    void remove_map()
    {
        bool removed = false;

        m_dynamic_enabled = true;
        for (auto& map : m_maps) {
            if (!map.valid || map.iova_base != m_map_iova) {
                continue;
            }
            map.valid = false;
            removed = true;
        }

        m_map_status = removed ? MAP_STATUS_OK : MAP_STATUS_ERROR;
        if (removed) {
            log_map("unmap", m_map_iova, m_map_pa, m_map_size);
        } else {
            log_map_error("unmap-miss", m_map_iova, m_map_pa, m_map_size);
        }
    }

    void log_map(const char* op, uint64_t iova, uint64_t pa, uint64_t len)
    {
        SCP_INFO(()) << "APOLLO_SMMU_TBU: " << op << " stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                     << " active=" << std::dec << map_count();
        std::cerr << "APOLLO_SMMU_TBU: " << op << " stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len << " active=" << std::dec
                  << map_count() << std::endl;
    }

    void log_map_error(const char* op, uint64_t iova, uint64_t pa, uint64_t len)
    {
        SCP_ERR(()) << "APOLLO_SMMU_TBU: " << op << " failed stream-id=0x" << std::hex
                    << p_stream_id.get_value() << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                    << " active=" << std::dec << map_count();
        std::cerr << "APOLLO_SMMU_TBU: " << op << " failed stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                  << " active=" << std::dec << map_count() << std::endl;
    }

    uint32_t read_reg(uint64_t addr) const
    {
        switch (addr) {
        case REG_MAP_IOVA_LO:
            return static_cast<uint32_t>(m_map_iova);
        case REG_MAP_IOVA_HI:
            return static_cast<uint32_t>(m_map_iova >> 32);
        case REG_MAP_PA_LO:
            return static_cast<uint32_t>(m_map_pa);
        case REG_MAP_PA_HI:
            return static_cast<uint32_t>(m_map_pa >> 32);
        case REG_MAP_SIZE_LO:
            return static_cast<uint32_t>(m_map_size);
        case REG_MAP_SIZE_HI:
            return static_cast<uint32_t>(m_map_size >> 32);
        case REG_MAP_STATUS:
            return m_map_status;
        case REG_MAP_COUNT:
            return map_count();
        default:
            return 0;
        }
    }

    void write_reg(uint64_t addr, uint32_t value)
    {
        switch (addr) {
        case REG_MAP_IOVA_LO:
            m_map_iova = (m_map_iova & 0xffffffff00000000ULL) | value;
            break;
        case REG_MAP_IOVA_HI:
            m_map_iova = (m_map_iova & 0xffffffffULL) | (static_cast<uint64_t>(value) << 32);
            break;
        case REG_MAP_PA_LO:
            m_map_pa = (m_map_pa & 0xffffffff00000000ULL) | value;
            break;
        case REG_MAP_PA_HI:
            m_map_pa = (m_map_pa & 0xffffffffULL) | (static_cast<uint64_t>(value) << 32);
            break;
        case REG_MAP_SIZE_LO:
            m_map_size = (m_map_size & 0xffffffff00000000ULL) | value;
            break;
        case REG_MAP_SIZE_HI:
            m_map_size = (m_map_size & 0xffffffffULL) | (static_cast<uint64_t>(value) << 32);
            break;
        case REG_MAP_CTRL:
            if (value == MAP_CTRL_ADD) {
                add_map();
            } else if (value == MAP_CTRL_REMOVE) {
                remove_map();
            } else {
                m_map_status = MAP_STATUS_ERROR;
                log_map_error("ctrl", m_map_iova, m_map_pa, m_map_size);
            }
            break;
        default:
            break;
        }
    }

    void regs_b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        uint32_t value = 0;
        const uint64_t addr = trans.get_address();

        (void)delay;
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        trans.set_dmi_allowed(false);

        if (trans.get_data_length() != sizeof(uint32_t)) {
            trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }

        if (trans.is_write()) {
            std::memcpy(&value, trans.get_data_ptr(), sizeof(value));
            write_reg(addr, value);
        } else if (trans.is_read()) {
            value = read_reg(addr);
            std::memcpy(trans.get_data_ptr(), &value, sizeof(value));
        } else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        }
    }

    unsigned int regs_transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        regs_b_transport(trans, delay);
        return trans.is_response_ok() ? trans.get_data_length() : 0;
    }

    void log_translate(const char* op, uint64_t iova, uint64_t pa, uint64_t len)
    {
        SCP_INFO(()) << "APOLLO_SMMU_TBU: stream-id=0x" << std::hex << p_stream_id.get_value()
                     << " translate " << op << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: stream-id=0x" << std::hex << p_stream_id.get_value() << " translate "
                  << op << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len << std::dec << std::endl;
    }

    void log_fault(const char* op, uint64_t iova, uint64_t len)
    {
        SCP_ERR(()) << "APOLLO_SMMU_TBU: translation fault stream-id=0x" << std::hex
                    << p_stream_id.get_value() << " " << op << " iova=0x" << iova << " len=0x" << len
                    << " window=[0x" << p_iova_base.get_value() << "..0x"
                    << (p_iova_base.get_value() + p_window_size.get_value()) << ")" << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: translation fault stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " " << op << " iova=0x" << iova << " len=0x" << len << " window=[0x"
                  << p_iova_base.get_value() << "..0x" << (p_iova_base.get_value() + p_window_size.get_value())
                  << ")" << std::dec << std::endl;
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        const uint64_t iova = trans.get_address();
        const uint64_t len = trans.get_data_length();
        const char* op = trans.is_read() ? "read" : (trans.is_write() ? "write" : "op");
        uint64_t pa = 0;

        if (!translate(iova, len, pa)) {
            log_fault(op, iova, len);
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        log_translate(op, iova, pa, len);
        trans.set_address(pa);
        downstream->b_transport(trans, delay);
        trans.set_address(iova);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        const uint64_t iova = trans.get_address();
        const uint64_t len = trans.get_data_length();
        uint64_t pa = 0;

        if (!translate(iova, len, pa)) {
            return 0;
        }

        trans.set_address(pa);
        const unsigned int ret = downstream->transport_dbg(trans);
        trans.set_address(iova);
        return ret;
    }
};

extern "C" void module_register();
