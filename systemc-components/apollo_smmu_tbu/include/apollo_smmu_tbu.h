/*
 * Apollo functional SMMU/TBU bridge.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

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
        REG_FEATURES = 0x24,
        REG_ATS_STATUS = 0x28,
        REG_PRI_STATUS = 0x2c,
        REG_FAULT_STATUS = 0x30,
        REG_FAULT_IOVA_LO = 0x34,
        REG_FAULT_IOVA_HI = 0x38,
        REG_FAULT_CTRL = 0x3c,
        REG_ARCH_TTBR_LO = 0x40,
        REG_ARCH_TTBR_HI = 0x44,
        REG_ARCH_IOVA_LO = 0x48,
        REG_ARCH_IOVA_HI = 0x4c,
        REG_ARCH_CTRL = 0x50,
        REG_ARCH_STATUS = 0x54,
        REG_ARCH_DESC_LO = 0x58,
        REG_ARCH_DESC_HI = 0x5c,
        REG_ARCH_PA_LO = 0x60,
        REG_ARCH_PA_HI = 0x64,
        REG_ARCH_LEVELS = 0x68,
        REG_ARCH_STE_BASE_LO = 0x6c,
        REG_ARCH_STE_BASE_HI = 0x70,
        REG_ARCH_STE_LO = 0x74,
        REG_ARCH_STE_HI = 0x78,
        REG_ARCH_CD_LO = 0x7c,
        REG_ARCH_CD_HI = 0x80,
        REG_ARCH_FAULT_REASON = 0x84,
        REG_ARCH_FAULT_REPLAY = 0x88,
        REG_ARCH_PROTOCOL_STATUS = 0x8c,
    };

    enum : uint32_t {
        MAP_CTRL_ADD = 1,
        MAP_CTRL_REMOVE = 2,
        MAP_CTRL_CLEAR = 3,
        FAULT_CTRL_CLEAR = 1,
        FAULT_CTRL_INJECT = 2,
        ARCH_CTRL_PROBE = 1,
        ARCH_CTRL_NEGATIVE_REPLAY = 2,
        MAP_STATUS_IDLE = 0,
        MAP_STATUS_OK = 1,
        MAP_STATUS_ERROR = 2,
        ARCH_STATUS_IDLE = 0,
        ARCH_STATUS_OK = 1,
        ARCH_STATUS_ERROR = 2,
        FEATURE_PAGE_TABLE_WALKER = 1u << 0,
        FEATURE_ATS_CACHE = 1u << 1,
        FEATURE_PRI_QUEUE = 1u << 2,
        FEATURE_FAULT_QUEUE = 1u << 3,
        FEATURE_ARCH_DESCRIPTOR_WALK = 1u << 4,
        FEATURE_ARCH_4_LEVEL_WALK = 1u << 5,
        FEATURE_STREAM_TABLE_WALK = 1u << 6,
        FEATURE_CONTEXT_DESCRIPTOR_WALK = 1u << 7,
        FEATURE_ARCH_FAULT_REPLAY = 1u << 8,
        FEATURE_ARCH_ATS_PRI_PROTOCOL = 1u << 9,
        PAGE_SHIFT = 12,
        PAGE_SIZE = 1u << PAGE_SHIFT,
    };

    enum : uint64_t {
        ARCH_INDEX_MASK = 0x1ff,
        ARCH_LEVELS = 4,
        ARCH_LEVEL_BITS = 9,
        ARCH_L0_SHIFT = 39,
        ARCH_DESC_TYPE_MASK = 0x3,
        ARCH_DESC_TABLE = 0x3,
        ARCH_DESC_PAGE = 0x3,
        ARCH_DESC_OUTPUT_MASK = 0x0000fffffffff000ULL,
        ARCH_STE_SIZE = 64,
        ARCH_CD_SIZE = 64,
        ARCH_STE_VALID = 1u << 0,
        ARCH_STE_S1_ENABLED = 1u << 1,
        ARCH_CD_VALID = 1u << 0,
        ARCH_FAULT_NONE = 0,
        ARCH_FAULT_STE_FETCH = 1,
        ARCH_FAULT_STE_INVALID = 2,
        ARCH_FAULT_CD_FETCH = 3,
        ARCH_FAULT_CD_INVALID = 4,
        ARCH_FAULT_TABLE_INVALID = 5,
        ARCH_FAULT_PAGE_INVALID = 6,
        ARCH_FAULT_NEGATIVE_UNEXPECTED_PASS = 7,
    };

    struct map_entry {
        uint64_t iova_base = 0;
        uint64_t pa_base = 0;
        uint64_t size = 0;
        bool valid = false;
    };

    std::array<map_entry, 64> m_maps {};
    std::array<uint64_t, 32> m_ats_cache {};
    uint64_t m_map_iova = 0;
    uint64_t m_map_pa = 0;
    uint64_t m_map_size = 0;
    uint64_t m_last_fault_iova = 0;
    uint64_t m_arch_ttbr = 0;
    uint64_t m_arch_ste_base = 0;
    uint64_t m_arch_cd_base = 0;
    uint64_t m_arch_iova = 0;
    uint64_t m_arch_last_desc = 0;
    uint64_t m_arch_last_pa = 0;
    uint64_t m_arch_last_ste = 0;
    uint64_t m_arch_last_cd = 0;
    uint32_t m_arch_walk_depth = 0;
    uint32_t m_arch_fault_reason = ARCH_FAULT_NONE;
    uint32_t m_arch_fault_replays = 0;
    uint32_t m_arch_ats_responses = 0;
    uint32_t m_arch_pri_responses = 0;
    uint32_t m_map_status = MAP_STATUS_IDLE;
    uint32_t m_arch_status = ARCH_STATUS_IDLE;
    uint32_t m_ats_entries = 0;
    uint32_t m_ats_fills = 0;
    uint32_t m_pri_requests = 0;
    uint32_t m_fault_count = 0;
    bool m_dynamic_enabled = false;

    bool translate_segment(uint64_t iova, uint64_t len, uint64_t& pa, uint64_t& segment_len)
    {
        if (m_dynamic_enabled) {
            for (const auto& map : m_maps) {
                if (!map.valid || len == 0 || iova < map.iova_base) {
                    continue;
                }

                const uint64_t offset = iova - map.iova_base;
                if (offset < map.size) {
                    pa = map.pa_base + offset;
                    segment_len = std::min({len, map.size - offset, page_remaining(iova)});
                    record_page_walk(iova, pa, segment_len);
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
        if (offset >= size) {
            return false;
        }

        pa = p_pa_base.get_value() + offset;
        segment_len = std::min({len, size - offset, page_remaining(iova)});
        record_page_walk(iova, pa, segment_len);
        return true;
    }

    bool translate(uint64_t iova, uint64_t len, uint64_t& pa)
    {
        uint64_t segment_len = 0;

        return translate_segment(iova, len, pa, segment_len) && segment_len == len;
    }

    static uint64_t page_base(uint64_t addr)
    {
        return addr & ~(static_cast<uint64_t>(PAGE_SIZE) - 1);
    }

    static uint64_t page_remaining(uint64_t addr)
    {
        return PAGE_SIZE - (addr & (PAGE_SIZE - 1));
    }

    static uint32_t page_count(uint64_t size)
    {
        return static_cast<uint32_t>((size + PAGE_SIZE - 1) >> PAGE_SHIFT);
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
                log_pri_resolved(m_map_iova, m_map_pa, m_map_size);
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
                log_pri_resolved(m_map_iova, m_map_pa, m_map_size);
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

    void clear_maps()
    {
        uint32_t removed = 0;

        m_dynamic_enabled = true;
        for (auto& map : m_maps) {
            if (map.valid) {
                map.valid = false;
                removed++;
            }
        }

        m_map_status = MAP_STATUS_OK;
        clear_ats_cache();
        SCP_INFO(()) << "APOLLO_SMMU_TBU: clear stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " removed=" << std::dec << removed
                     << " active=" << map_count();
        std::cerr << "APOLLO_SMMU_TBU: clear stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " removed=" << std::dec << removed << " active=" << map_count() << std::endl;
    }

    uint32_t features() const
    {
        return FEATURE_PAGE_TABLE_WALKER | FEATURE_ATS_CACHE | FEATURE_PRI_QUEUE | FEATURE_FAULT_QUEUE |
               FEATURE_ARCH_DESCRIPTOR_WALK | FEATURE_ARCH_4_LEVEL_WALK | FEATURE_STREAM_TABLE_WALK |
               FEATURE_CONTEXT_DESCRIPTOR_WALK | FEATURE_ARCH_FAULT_REPLAY | FEATURE_ARCH_ATS_PRI_PROTOCOL;
    }

    uint32_t ats_status() const
    {
        return ((m_ats_fills & 0xffffu) << 16) | (m_ats_entries & 0xffffu);
    }

    uint32_t pri_status() const
    {
        return m_pri_requests;
    }

    uint32_t fault_status() const
    {
        return m_fault_count;
    }

    void clear_ats_cache()
    {
        m_ats_cache.fill(std::numeric_limits<uint64_t>::max());
        m_ats_entries = 0;
    }

    bool ats_lookup(uint64_t page) const
    {
        for (uint32_t i = 0; i < m_ats_entries; i++) {
            if (m_ats_cache[i] == page) {
                return true;
            }
        }
        return false;
    }

    void ats_fill(uint64_t page)
    {
        if (ats_lookup(page)) {
            return;
        }
        if (m_ats_entries < m_ats_cache.size()) {
            m_ats_cache[m_ats_entries++] = page;
        } else {
            m_ats_cache[m_ats_fills % m_ats_cache.size()] = page;
        }
        m_ats_fills++;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS cache fill stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " iova-page=0x" << page << " entries=" << std::dec
                     << m_ats_entries;
        std::cerr << "APOLLO_SMMU_TBU: ATS cache fill stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " iova-page=0x" << page << " entries=" << std::dec << m_ats_entries << std::endl;
    }

    void record_page_walk(uint64_t iova, uint64_t pa, uint64_t len)
    {
        const uint64_t page = page_base(iova);

        ats_fill(page);
        SCP_INFO(()) << "APOLLO_SMMU_TBU: page-table walk stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: page-table walk stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len << std::dec << std::endl;
    }

    void log_pri_resolved(uint64_t iova, uint64_t pa, uint64_t len)
    {
        m_pri_requests += page_count(len);
        SCP_INFO(()) << "APOLLO_SMMU_TBU: PRI request resolved stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                     << " pages=" << std::dec << page_count(len);
        std::cerr << "APOLLO_SMMU_TBU: PRI request resolved stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                  << " pages=" << std::dec << page_count(len) << std::endl;
    }

    void record_fault(const char* op, uint64_t iova, uint64_t len)
    {
        m_last_fault_iova = iova;
        m_fault_count++;
        SCP_WARN(()) << "APOLLO_SMMU_TBU: fault queue push stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " " << op << " iova=0x" << iova << " len=0x" << len
                     << " count=" << std::dec << m_fault_count;
        std::cerr << "APOLLO_SMMU_TBU: fault queue push stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " " << op << " iova=0x" << iova << " len=0x" << len << " count=" << std::dec
                  << m_fault_count << std::endl;
    }

    void clear_faults()
    {
        m_fault_count = 0;
        m_last_fault_iova = 0;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: fault queue cleared stream-id=0x" << std::hex
                     << p_stream_id.get_value() << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: fault queue cleared stream-id=0x" << std::hex
                  << p_stream_id.get_value() << std::dec << std::endl;
    }

    void inject_fault()
    {
        record_fault("probe", m_map_iova, m_map_size ? m_map_size : PAGE_SIZE);
        m_map_status = MAP_STATUS_OK;
    }

    bool read_downstream_u64(uint64_t pa, uint64_t& value)
    {
        std::array<uint8_t, sizeof(value)> data {};
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(pa);
        trans.set_data_ptr(data.data());
        trans.set_data_length(data.size());
        trans.set_streaming_width(data.size());
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        downstream->b_transport(trans, delay);
        if (!trans.is_response_ok()) {
            SCP_WARN(()) << "APOLLO_SMMU_TBU: descriptor fetch failed stream-id=0x" << std::hex
                         << p_stream_id.get_value() << " pa=0x" << pa << " response="
                         << trans.get_response_string();
            std::cerr << "APOLLO_SMMU_TBU: descriptor fetch failed stream-id=0x" << std::hex
                      << p_stream_id.get_value() << " pa=0x" << pa << " response="
                      << trans.get_response_string() << std::dec << std::endl;
            return false;
        }

        std::memcpy(&value, data.data(), sizeof(value));
        return true;
    }

    static uint64_t arch_level_index(uint64_t iova, uint32_t level)
    {
        return (iova >> (ARCH_L0_SHIFT - level * ARCH_LEVEL_BITS)) & ARCH_INDEX_MASK;
    }

    static const char* arch_fault_reason_name(uint32_t reason)
    {
        switch (reason) {
        case ARCH_FAULT_NONE:
            return "none";
        case ARCH_FAULT_STE_FETCH:
            return "stream-descriptor-fetch";
        case ARCH_FAULT_STE_INVALID:
            return "stream-descriptor-invalid";
        case ARCH_FAULT_CD_FETCH:
            return "context-descriptor-fetch";
        case ARCH_FAULT_CD_INVALID:
            return "context-descriptor-invalid";
        case ARCH_FAULT_TABLE_INVALID:
            return "table-descriptor-invalid";
        case ARCH_FAULT_PAGE_INVALID:
            return "page-descriptor-invalid";
        case ARCH_FAULT_NEGATIVE_UNEXPECTED_PASS:
            return "negative-unexpected-pass";
        default:
            return "unknown";
        }
    }

    uint32_t arch_protocol_status() const
    {
        return ((m_arch_pri_responses & 0xffffu) << 16) | (m_arch_ats_responses & 0xffffu);
    }

    bool arch_descriptor_walk(uint64_t iova, uint64_t& pa, uint64_t& desc)
    {
        uint64_t table_pa = m_arch_ttbr;
        uint64_t desc_pa = 0;

        if (table_pa == 0) {
            m_arch_fault_reason = ARCH_FAULT_TABLE_INVALID;
            return false;
        }

        for (uint32_t level = 0; level < ARCH_LEVELS; level++) {
            const uint64_t index = arch_level_index(iova, level);

            desc_pa = table_pa + index * sizeof(desc);
            if (!read_downstream_u64(desc_pa, desc)) {
                m_arch_fault_reason = (level + 1 < ARCH_LEVELS) ? ARCH_FAULT_TABLE_INVALID :
                                                                 ARCH_FAULT_PAGE_INVALID;
                return false;
            }

            if (level + 1 < ARCH_LEVELS) {
                if ((desc & ARCH_DESC_TYPE_MASK) != ARCH_DESC_TABLE) {
                    m_arch_fault_reason = ARCH_FAULT_TABLE_INVALID;
                    SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural table descriptor stream-id=0x"
                                 << std::hex << p_stream_id.get_value() << " level=" << std::dec << level
                                 << std::hex << " desc-pa=0x" << desc_pa << " desc=0x" << desc;
                    std::cerr << "APOLLO_SMMU_TBU: invalid architectural table descriptor stream-id=0x"
                              << std::hex << p_stream_id.get_value() << " level=" << std::dec << level
                              << std::hex << " desc-pa=0x" << desc_pa << " desc=0x" << desc << std::dec
                              << std::endl;
                    return false;
                }

                table_pa = desc & ARCH_DESC_OUTPUT_MASK;
                SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural table walk stream-id=0x" << std::hex
                             << p_stream_id.get_value() << " level=" << std::dec << level << std::hex
                             << " table=0x" << (desc_pa - index * sizeof(desc)) << " index=0x" << index
                             << " desc-pa=0x" << desc_pa << " desc=0x" << desc << " next-table=0x" << table_pa
                             << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architectural table walk stream-id=0x" << std::hex
                          << p_stream_id.get_value() << " level=" << std::dec << level << std::hex
                          << " table=0x" << (desc_pa - index * sizeof(desc)) << " index=0x" << index
                          << " desc-pa=0x" << desc_pa << " desc=0x" << desc << " next-table=0x" << table_pa
                          << std::dec << std::endl;
                continue;
            }

            if ((desc & ARCH_DESC_TYPE_MASK) != ARCH_DESC_PAGE) {
                m_arch_fault_reason = ARCH_FAULT_PAGE_INVALID;
                SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural page descriptor stream-id=0x"
                             << std::hex << p_stream_id.get_value() << " desc-pa=0x" << desc_pa
                             << " desc=0x" << desc;
                std::cerr << "APOLLO_SMMU_TBU: invalid architectural page descriptor stream-id=0x"
                          << std::hex << p_stream_id.get_value() << " desc-pa=0x" << desc_pa << " desc=0x"
                          << desc << std::dec << std::endl;
                return false;
            }
        }

        pa = (desc & ARCH_DESC_OUTPUT_MASK) | (iova & (PAGE_SIZE - 1));
        ats_fill(page_base(iova));
        m_arch_walk_depth = ARCH_LEVELS;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural descriptor walk stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " levels=" << std::dec << ARCH_LEVELS << std::hex
                     << " ttbr=0x" << m_arch_ttbr << " desc-pa=0x" << desc_pa << " desc=0x" << desc
                     << " iova=0x" << iova << " pa=0x" << pa << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural descriptor walk stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " levels=" << std::dec << ARCH_LEVELS << std::hex
                  << " ttbr=0x" << m_arch_ttbr << " desc-pa=0x" << desc_pa << " desc=0x" << desc
                  << " iova=0x" << iova << " pa=0x" << pa << std::dec << std::endl;
        return true;
    }

    bool arch_stream_context_walk(uint64_t iova, uint64_t& pa, uint64_t& desc)
    {
        uint64_t ste0 = 0;
        uint64_t ste1 = 0;
        uint64_t cd0 = 0;
        uint64_t cd1 = 0;
        const uint64_t ste_pa = m_arch_ste_base + p_stream_id.get_value() * ARCH_STE_SIZE;

        if (m_arch_ste_base == 0) {
            return arch_descriptor_walk(iova, pa, desc);
        }

        if (!read_downstream_u64(ste_pa, ste0) || !read_downstream_u64(ste_pa + sizeof(ste0), ste1)) {
            m_arch_fault_reason = ARCH_FAULT_STE_FETCH;
            return false;
        }

        m_arch_last_ste = ste0;
        m_arch_cd_base = ste1 & ARCH_DESC_OUTPUT_MASK;
        if ((ste0 & (ARCH_STE_VALID | ARCH_STE_S1_ENABLED)) !=
                (ARCH_STE_VALID | ARCH_STE_S1_ENABLED) ||
            m_arch_cd_base == 0) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural stream descriptor stream-id=0x"
                         << std::hex << p_stream_id.get_value() << " ste-pa=0x" << ste_pa << " ste=0x"
                         << ste0 << " cd-table=0x" << m_arch_cd_base << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: invalid architectural stream descriptor stream-id=0x"
                      << std::hex << p_stream_id.get_value() << " ste-pa=0x" << ste_pa << " ste=0x"
                      << ste0 << " cd-table=0x" << m_arch_cd_base << std::dec << std::endl;
            return false;
        }

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural stream table walk stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " ste-pa=0x" << ste_pa << " ste=0x" << ste0
                     << " cd-table=0x" << m_arch_cd_base << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural stream table walk stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " ste-pa=0x" << ste_pa << " ste=0x" << ste0
                  << " cd-table=0x" << m_arch_cd_base << std::dec << std::endl;

        const uint64_t cd_pa = m_arch_cd_base;
        if (!read_downstream_u64(cd_pa, cd0) || !read_downstream_u64(cd_pa + sizeof(cd0), cd1)) {
            m_arch_fault_reason = ARCH_FAULT_CD_FETCH;
            return false;
        }

        m_arch_last_cd = cd0;
        m_arch_ttbr = cd1 & ARCH_DESC_OUTPUT_MASK;
        if ((cd0 & ARCH_CD_VALID) == 0 || m_arch_ttbr == 0) {
            m_arch_fault_reason = ARCH_FAULT_CD_INVALID;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural context descriptor stream-id=0x"
                         << std::hex << p_stream_id.get_value() << " cd-pa=0x" << cd_pa << " cd=0x" << cd0
                         << " ttbr=0x" << m_arch_ttbr << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: invalid architectural context descriptor stream-id=0x"
                      << std::hex << p_stream_id.get_value() << " cd-pa=0x" << cd_pa << " cd=0x" << cd0
                      << " ttbr=0x" << m_arch_ttbr << std::dec << std::endl;
            return false;
        }

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural context descriptor walk stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " cd-pa=0x" << cd_pa << " cd=0x" << cd0
                     << " ttbr=0x" << m_arch_ttbr << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural context descriptor walk stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " cd-pa=0x" << cd_pa << " cd=0x" << cd0
                  << " ttbr=0x" << m_arch_ttbr << std::dec << std::endl;

        return arch_descriptor_walk(iova, pa, desc);
    }

    void log_arch_protocol_response(uint64_t iova, uint64_t pa, bool success)
    {
        m_arch_ats_responses++;
        m_arch_pri_responses++;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected ATS translation response stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " iova=0x" << iova << " pa=0x" << pa
                     << " status=" << (success ? "success" : "fault")
                     << " reason=" << arch_fault_reason_name(m_arch_fault_reason) << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected ATS translation response stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " iova=0x" << iova << " pa=0x" << pa
                  << " status=" << (success ? "success" : "fault")
                  << " reason=" << arch_fault_reason_name(m_arch_fault_reason) << std::dec << std::endl;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected PRI response stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " iova=0x" << iova
                     << " status=" << (success ? "success" : "fault")
                     << " reason=" << arch_fault_reason_name(m_arch_fault_reason) << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected PRI response stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " iova=0x" << iova
                  << " status=" << (success ? "success" : "fault")
                  << " reason=" << arch_fault_reason_name(m_arch_fault_reason) << std::dec << std::endl;
    }

    void run_arch_probe()
    {
        uint64_t desc = 0;
        uint64_t pa = 0;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_walk_depth = 0;
        m_arch_last_desc = 0;
        m_arch_last_pa = 0;
        m_arch_last_ste = 0;
        m_arch_last_cd = 0;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        if (!arch_stream_context_walk(m_arch_iova, pa, desc)) {
            log_arch_protocol_response(m_arch_iova, 0, false);
            record_fault("architectural-probe", m_arch_iova, PAGE_SIZE);
            return;
        }

        m_arch_last_desc = desc;
        m_arch_last_pa = pa;
        m_pri_requests++;
        log_arch_protocol_response(m_arch_iova, pa, true);
        m_arch_status = ARCH_STATUS_OK;
    }

    void run_arch_negative_replay()
    {
        uint64_t desc = 0;
        uint64_t pa = 0;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_walk_depth = 0;
        m_arch_last_desc = 0;
        m_arch_last_pa = 0;
        m_arch_last_ste = 0;
        m_arch_last_cd = 0;
        m_arch_fault_reason = ARCH_FAULT_NONE;

        if (arch_stream_context_walk(m_arch_iova, pa, desc)) {
            m_arch_last_desc = desc;
            m_arch_last_pa = pa;
            m_arch_fault_reason = ARCH_FAULT_NEGATIVE_UNEXPECTED_PASS;
        }

        m_arch_fault_replays++;
        log_arch_protocol_response(m_arch_iova, pa, false);
        record_fault("architected-negative-replay", m_arch_iova, PAGE_SIZE);
        SCP_WARN(()) << "APOLLO_SMMU_TBU: architected fault replay queued stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                     << " replay=" << std::dec << m_arch_fault_replays << std::hex << " iova=0x"
                     << m_arch_iova << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected fault replay queued stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                  << " replay=" << std::dec << m_arch_fault_replays << std::hex << " iova=0x" << m_arch_iova
                  << std::dec << std::endl;
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
        case REG_FEATURES:
            return features();
        case REG_ATS_STATUS:
            return ats_status();
        case REG_PRI_STATUS:
            return pri_status();
        case REG_FAULT_STATUS:
            return fault_status();
        case REG_FAULT_IOVA_LO:
            return static_cast<uint32_t>(m_last_fault_iova);
        case REG_FAULT_IOVA_HI:
            return static_cast<uint32_t>(m_last_fault_iova >> 32);
        case REG_ARCH_TTBR_LO:
            return static_cast<uint32_t>(m_arch_ttbr);
        case REG_ARCH_TTBR_HI:
            return static_cast<uint32_t>(m_arch_ttbr >> 32);
        case REG_ARCH_IOVA_LO:
            return static_cast<uint32_t>(m_arch_iova);
        case REG_ARCH_IOVA_HI:
            return static_cast<uint32_t>(m_arch_iova >> 32);
        case REG_ARCH_STATUS:
            return m_arch_status;
        case REG_ARCH_DESC_LO:
            return static_cast<uint32_t>(m_arch_last_desc);
        case REG_ARCH_DESC_HI:
            return static_cast<uint32_t>(m_arch_last_desc >> 32);
        case REG_ARCH_PA_LO:
            return static_cast<uint32_t>(m_arch_last_pa);
        case REG_ARCH_PA_HI:
            return static_cast<uint32_t>(m_arch_last_pa >> 32);
        case REG_ARCH_LEVELS:
            return m_arch_walk_depth;
        case REG_ARCH_STE_BASE_LO:
            return static_cast<uint32_t>(m_arch_ste_base);
        case REG_ARCH_STE_BASE_HI:
            return static_cast<uint32_t>(m_arch_ste_base >> 32);
        case REG_ARCH_STE_LO:
            return static_cast<uint32_t>(m_arch_last_ste);
        case REG_ARCH_STE_HI:
            return static_cast<uint32_t>(m_arch_last_ste >> 32);
        case REG_ARCH_CD_LO:
            return static_cast<uint32_t>(m_arch_last_cd);
        case REG_ARCH_CD_HI:
            return static_cast<uint32_t>(m_arch_last_cd >> 32);
        case REG_ARCH_FAULT_REASON:
            return m_arch_fault_reason;
        case REG_ARCH_FAULT_REPLAY:
            return m_arch_fault_replays;
        case REG_ARCH_PROTOCOL_STATUS:
            return arch_protocol_status();
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
            } else if (value == MAP_CTRL_CLEAR) {
                clear_maps();
            } else {
                m_map_status = MAP_STATUS_ERROR;
                log_map_error("ctrl", m_map_iova, m_map_pa, m_map_size);
            }
            break;
        case REG_FAULT_CTRL:
            if (value == FAULT_CTRL_CLEAR) {
                clear_faults();
            } else if (value == FAULT_CTRL_INJECT) {
                inject_fault();
            }
            break;
        case REG_ARCH_TTBR_LO:
            m_arch_ttbr = (m_arch_ttbr & 0xffffffff00000000ULL) | value;
            break;
        case REG_ARCH_TTBR_HI:
            m_arch_ttbr = (m_arch_ttbr & 0xffffffffULL) | (static_cast<uint64_t>(value) << 32);
            break;
        case REG_ARCH_IOVA_LO:
            m_arch_iova = (m_arch_iova & 0xffffffff00000000ULL) | value;
            break;
        case REG_ARCH_IOVA_HI:
            m_arch_iova = (m_arch_iova & 0xffffffffULL) | (static_cast<uint64_t>(value) << 32);
            break;
        case REG_ARCH_STE_BASE_LO:
            m_arch_ste_base = (m_arch_ste_base & 0xffffffff00000000ULL) | value;
            break;
        case REG_ARCH_STE_BASE_HI:
            m_arch_ste_base = (m_arch_ste_base & 0xffffffffULL) | (static_cast<uint64_t>(value) << 32);
            break;
        case REG_ARCH_CTRL:
            if (value == ARCH_CTRL_PROBE) {
                run_arch_probe();
            } else if (value == ARCH_CTRL_NEGATIVE_REPLAY) {
                run_arch_negative_replay();
            } else {
                m_arch_status = ARCH_STATUS_ERROR;
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
        record_fault(op, iova, len);
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
        auto* const data = trans.get_data_ptr();
        const unsigned int streaming_width = trans.get_streaming_width();
        uint64_t offset = 0;

        while (offset < len) {
            uint64_t pa = 0;
            uint64_t segment_len = 0;

            if (!translate_segment(iova + offset, len - offset, pa, segment_len)) {
                log_fault(op, iova + offset, len - offset);
                trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                break;
            }

            log_translate(op, iova + offset, pa, segment_len);
            trans.set_address(pa);
            trans.set_data_ptr(data + offset);
            trans.set_data_length(segment_len);
            trans.set_streaming_width(segment_len);
            downstream->b_transport(trans, delay);
            if (!trans.is_response_ok()) {
                break;
            }
            offset += segment_len;
        }

        trans.set_address(iova);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(streaming_width);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        const uint64_t iova = trans.get_address();
        const uint64_t len = trans.get_data_length();
        auto* const data = trans.get_data_ptr();
        const unsigned int streaming_width = trans.get_streaming_width();
        uint64_t offset = 0;
        unsigned int total = 0;

        while (offset < len) {
            uint64_t pa = 0;
            uint64_t segment_len = 0;
            unsigned int ret = 0;

            if (!translate_segment(iova + offset, len - offset, pa, segment_len)) {
                break;
            }

            trans.set_address(pa);
            trans.set_data_ptr(data + offset);
            trans.set_data_length(segment_len);
            trans.set_streaming_width(segment_len);
            ret = downstream->transport_dbg(trans);
            total += ret;
            if (ret != segment_len) {
                break;
            }
            offset += segment_len;
        }

        trans.set_address(iova);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(streaming_width);
        return total;
    }
};

extern "C" void module_register();
