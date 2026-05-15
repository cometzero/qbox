/*
 * Apollo SMMUv3 architected core ownership contract.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <tlm>

namespace apollo::smmuv3 {

class apollo_smmu_arch_core
{
public:
    enum class translation_owner : uint8_t {
        apollo_systemc,
        qemu_bridge_pending,
    };

    enum class register_aperture : uint8_t {
        compatibility,
        smmuv3,
        invalid,
    };

    struct ownership_contract {
        translation_owner owner = translation_owner::apollo_systemc;
        bool compatibility_adapter_required = true;
        bool qemu_translation_bridge_enabled = false;
    };

    struct state_ownership_contract {
        bool register_queue_state = true;
        bool stream_context_descriptor_state = true;
        bool page_table_walker_state = true;
        bool fault_replay_state = true;
    };

    struct queue_state {
        uint64_t base = 0;
        uint32_t prod = 0;
        uint32_t cons = 0;
        bool overflow = false;
        bool ovflg = false;
        bool ovackflg = false;
    };

    struct stream_context_descriptor_state {
        uint64_t compatibility_ste_base = 0;
        uint64_t current_cd_base = 0;
        uint64_t strtab_base = 0;
        uint32_t strtab_cfg = 0;
        uint32_t selected_stream_id = 0;
        bool selected_stream_id_valid = false;
        uint32_t selected_ssid = 0;
        bool selected_ssid_valid = false;
    };

    struct page_table_walker_state {
        uint64_t ttbr = 0;
        uint64_t iova = 0;
        uint64_t s2ttb = 0;
        uint64_t last_desc = 0;
        uint64_t last_pa = 0;
        uint64_t last_ipa = 0;
        uint64_t last_fetch_addr = 0;
        uint32_t walk_depth = 0;
        uint32_t last_stage = 0;
    };

    struct fault_replay_state {
        uint32_t fault_reason = 0;
        uint32_t fault_stage = 0;
        uint32_t fault_event_class = 2;
        bool fault_nsipa = false;
        bool fault_gpcf = false;
        bool fault_record_suppressed = false;
        uint32_t last_fault_detail = 0;
        uint32_t fault_replays = 0;
        uint32_t ats_responses = 0;
        uint32_t pri_responses = 0;
        uint32_t ats_success = 0;
        uint32_t ats_ur = 0;
        uint32_t ats_ca = 0;
        uint32_t pri_accepted = 0;
        uint32_t pri_rejected = 0;
        uint32_t pri_unknown = 0;
        uint32_t pri_auto_responses = 0;
        uint32_t pri_auto_failures = 0;
        uint32_t stall_pending = 0;
        uint32_t stall_retried = 0;
        uint32_t stall_terminated = 0;
        uint32_t stall_buffered = 0;
        uint32_t stall_redriven = 0;
        uint32_t stall_suppressed = 0;
        uint32_t stall_merged = 0;
        uint32_t resume_unknown = 0;
        uint32_t endpoint_replay_pending = 0;
        uint32_t endpoint_replay_retried = 0;
        uint32_t endpoint_replay_succeeded = 0;
        uint32_t endpoint_replay_terminated = 0;
        uint32_t endpoint_replay_redriven = 0;
        uint32_t endpoint_replay_failed = 0;
        uint32_t endpoint_block_waits = 0;
        uint32_t endpoint_block_resumed = 0;
        uint32_t endpoint_block_failed = 0;
        bool endpoint_blocking_enabled = false;
        uint32_t early_retry_attempted = 0;
        uint32_t early_retry_succeeded = 0;
        uint32_t early_retry_failed = 0;
        uint32_t early_retry_discarded = 0;
        uint32_t next_fault_replay_id = 1;
        uint16_t next_stag = 1;
        uint16_t last_stag = 0;
        uint16_t last_resume_stag = 0;
        uint16_t next_prg = 1;
        uint16_t last_prg = 0;
        uint16_t last_auto_prg = 0;
        uint8_t last_auto_response = 0;
        uint32_t last_pri_response_stream_id = 0;
        uint8_t last_pri_response_code = 0;
        uint8_t last_pri_response_ats_status = 0;
        bool last_pri_response_valid = false;
        bool last_pri_response_unknown = false;
        bool last_pri_response_stream_mismatch = false;
        uint32_t last_pri_response_cmd_stream_id = 0;
        bool last_pri_response_ssid_valid = false;
        uint32_t last_pri_response_ssid = 0;
        bool last_pri_response_ssid_mismatch = false;
        bool last_pri_response_cmd_ssid_valid = false;
        uint32_t last_pri_response_cmd_ssid = 0;
        bool last_pri_response_order_mismatch = false;
        uint16_t last_pri_response_head_prg = 0;
    };

    struct stall_record {
        uint32_t stream_id = 0;
        uint16_t stag = 0;
        uint64_t iova = 0;
        uint32_t ssid = 0;
        bool ssid_valid = false;
        bool pending = false;
        bool event_committed = false;
    };

    struct endpoint_replay_record {
        uint32_t stream_id = 0;
        uint16_t stag = 0;
        uint64_t iova = 0;
        uint64_t len = 0;
        uint64_t replay_pa = 0;
        uint64_t replay_len = 0;
        uint32_t ssid = 0;
        bool ssid_valid = false;
        bool write = false;
        bool pending = false;
        bool replayed = false;
        bool redriven = false;
        bool succeeded = false;
        bool early_retry_succeeded = false;
        tlm::tlm_response_status replay_status = tlm::TLM_INCOMPLETE_RESPONSE;
        std::vector<uint8_t> payload {};
    };

    struct endpoint_replay_allocation {
        endpoint_replay_record* record = nullptr;
        bool allocated = false;
        bool duplicate = false;
        bool invalid_stag = false;
        bool capacity_exhausted = false;
    };

    struct endpoint_replay_segment {
        uint64_t offset = 0;
        uint64_t pa = 0;
        uint64_t len = 0;
        uint8_t* payload = nullptr;
        tlm::tlm_response_status status = tlm::TLM_INCOMPLETE_RESPONSE;
        bool valid = false;
        bool first_segment = false;
    };

    struct endpoint_replay_transaction {
        uint64_t pa = 0;
        uint64_t len = 0;
        uint8_t* payload = nullptr;
        bool write = false;
        bool valid = false;
    };

    struct descriptor_walk_config {
        uint32_t granule = 0;
        uint32_t start_level = 0;
        uint32_t levels = 4;
        uint32_t stage = 1;
    };

    struct descriptor_fetch {
        uint64_t table_pa = 0;
        uint64_t index = 0;
        uint64_t desc_pa = 0;
    };

    struct descriptor_memory_read {
        uint64_t desc_pa = 0;
        uint64_t transaction_pa = 0;
        uint32_t stage = 1;
        bool stage2_translated = false;
    };

    enum class descriptor_step_kind : uint8_t {
        fault,
        table,
        leaf,
    };

    struct descriptor_step {
        descriptor_step_kind kind = descriptor_step_kind::fault;
        uint64_t next_table_pa = 0;
        uint64_t pa = 0;
        uint32_t fault_reason = FAULT_NONE;
        bool block_leaf = false;
        bool block_nt = false;
    };

    static constexpr uint32_t ABI_VERSION = 1;
    static constexpr uint64_t COMPATIBILITY_APERTURE_BASE = 0x0000;
    static constexpr uint64_t SMMUV3_APERTURE_BASE = 0x1000;
    static constexpr uint64_t SMMUV3_APERTURE_SIZE = 0x40000;
    static constexpr uint64_t QUEUE_BASE_MASK = 0x0000ffffffffffe0ULL;
    static constexpr uint32_t QUEUE_OVFLG = 1u << 31;
    static constexpr uint32_t QUEUE_INDEX_MASK = QUEUE_OVFLG - 1;
    static constexpr uint32_t WALKER_GRANULE_4K = 0;
    static constexpr uint32_t WALKER_GRANULE_16K = 1;
    static constexpr uint32_t WALKER_GRANULE_64K = 2;
    static constexpr uint32_t WALKER_LEVELS = 4;
    static constexpr uint32_t WALKER_LEVEL_BITS = 9;
    static constexpr uint32_t WALKER_INDEX_MASK = 0x1ff;
    static constexpr uint32_t WALKER_L0_SHIFT = 39;
    static constexpr uint64_t WALKER_DESC_TYPE_MASK = 0x3;
    static constexpr uint64_t WALKER_DESC_BLOCK = 0x1;
    static constexpr uint64_t WALKER_DESC_TABLE = 0x3;
    static constexpr uint64_t WALKER_DESC_PAGE = 0x3;
    static constexpr uint64_t WALKER_DESC_OUTPUT_MASK = 0x0000fffffffff000ULL;
    static constexpr uint64_t WALKER_DESC_AF = 1ULL << 10;
    static constexpr uint64_t WALKER_DESC_AP_RO = 1ULL << 7;
    static constexpr uint64_t WALKER_DESC_NT = 1ULL << 16;
    static constexpr uint64_t WALKER_DESC_DBM = 1ULL << 51;
    static constexpr uint64_t STE_VALID = 1u << 0;
    static constexpr uint64_t STE_S1_ENABLED = 1u << 1;
    static constexpr uint32_t STE_CFG_SHIFT = 1;
    static constexpr uint32_t STE_CFG_MASK = 0x7;
    static constexpr uint32_t STE_CFG_BYPASS = 0x4;
    static constexpr uint32_t STE_CFG_ABORT = STE_CFG_BYPASS;
    static constexpr uint32_t STE_CFG_S1_TRANS = 0x5;
    static constexpr uint32_t STE_CFG_S2_TRANS = 0x6;
    static constexpr uint32_t STE_CFG_NESTED = 0x7;
    static constexpr uint64_t STE_S1CTXPTR_MASK = 0x0000ffffffffffc0ULL;
    static constexpr uint64_t STE_S2TTB_MASK = 0x0000fffffffffff0ULL;
    static constexpr uint32_t STE_S1FMT_SHIFT = 4;
    static constexpr uint32_t STE_S1FMT_MASK = 0x3;
    static constexpr uint32_t STE_S1DSS_MASK = 0x3;
    static constexpr uint32_t STE_EATS_SHIFT = 28;
    static constexpr uint32_t STE_EATS_MASK = 0x3;
    static constexpr uint32_t STE_EATS_DISABLED = 0;
    static constexpr uint32_t STE_EATS_SPLIT = 2;
    static constexpr uint32_t STE_EATS_DPT = 3;
    static constexpr uint64_t STE_S1MPAM = 1ULL << 26;
    static constexpr uint64_t STE_VMSPTR_MASK = WALKER_DESC_OUTPUT_MASK;
    static constexpr uint32_t STE_PARTID_SHIFT = 16;
    static constexpr uint32_t STE_PARTID_MASK = 0xffff;
    static constexpr uint32_t STE_PMG_SHIFT = 0;
    static constexpr uint32_t STE_PMG_MASK = 0xff;
    static constexpr uint32_t STE_S1CDMAX_SHIFT = 59;
    static constexpr uint32_t STE_S1CDMAX_MASK = 0x1f;
    static constexpr uint64_t CD_VALID = 1u << 0;
    static constexpr uint64_t CD_VALID_ARCHITECTED = 1ULL << 31;
    static constexpr uint64_t CD_TTB0_MASK = 0x0000fffffffffff0ULL;
    static constexpr uint32_t CD_ASID_SHIFT = 48;
    static constexpr uint64_t CD_ASID_MASK = 0xffffULL << CD_ASID_SHIFT;
    static constexpr uint32_t CD_PARTID_SHIFT = 32;
    static constexpr uint32_t CD_PARTID_MASK = 0xffff;
    static constexpr uint32_t CD_PMG_SHIFT = 48;
    static constexpr uint32_t CD_PMG_MASK = 0xff;
    static constexpr uint64_t STRTAB_BASE_ADDR_MASK = 0x0000ffffffffffc0ULL;
    static constexpr uint32_t STRTAB_FMT_LINEAR = 0;
    static constexpr uint32_t STRTAB_FMT_2LVL = 1;
    static constexpr uint32_t STRTAB_CFG_LOG2SIZE_MASK = 0x3f;
    static constexpr uint32_t STRTAB_CFG_SPLIT_SHIFT = 6;
    static constexpr uint32_t STRTAB_CFG_SPLIT_MASK = 0x1f;
    static constexpr uint32_t STRTAB_CFG_FMT_SHIFT = 16;
    static constexpr uint32_t STRTAB_CFG_FMT_MASK = 0x3;
    static constexpr uint32_t STRTAB_L1_DESC_SPAN_MASK = 0x1f;
    static constexpr uint64_t STRTAB_L1_DESC_L2PTR_MASK = 0x0000ffffffffffc0ULL;
    static constexpr uint32_t FAULT_NONE = 0;
    static constexpr uint32_t FAULT_STE_FETCH = 1;
    static constexpr uint32_t FAULT_STE_INVALID = 2;
    static constexpr uint32_t FAULT_CD_FETCH = 3;
    static constexpr uint32_t FAULT_CD_INVALID = 4;
    static constexpr uint32_t FAULT_TABLE_INVALID = 5;
    static constexpr uint32_t FAULT_PAGE_INVALID = 6;
    static constexpr uint32_t FAULT_NEGATIVE_UNEXPECTED_PASS = 7;
    static constexpr uint32_t FAULT_BAD_STREAM_ID = 8;
    static constexpr uint32_t FAULT_ACCESS = 9;
    static constexpr uint32_t FAULT_PERMISSION = 10;
    static constexpr uint32_t FAULT_ADDR_SIZE = 11;
    static constexpr uint32_t FAULT_GRANULE = 12;
    static constexpr uint32_t FAULT_STAGE2 = 13;
    static constexpr uint32_t FAULT_BAD_ATS_TREQ = 14;
    static constexpr uint32_t FAULT_WALK_EABT = 15;
    static constexpr uint32_t FAULT_VMS_FETCH = 16;
    static constexpr uint32_t FAULT_STREAM_DISABLED = 17;
    static constexpr uint32_t FAULT_BAD_SUBSTREAMID = 18;
    static constexpr uint32_t FAULT_TRANSL_FORBIDDEN = 19;
    static constexpr uint32_t FAULT_TLB_CONFLICT = 20;
    static constexpr uint32_t FAULT_CFG_CONFLICT = 21;
    static constexpr uint32_t FAULT_UNSUPPORTED_UPSTREAM = 22;
    static constexpr uint32_t FAULT_CLASS_NONE = 0;
    static constexpr uint32_t FAULT_CLASS_STE = 1;
    static constexpr uint32_t FAULT_CLASS_CD = 2;
    static constexpr uint32_t FAULT_CLASS_TRANSLATION = 3;
    static constexpr uint32_t FAULT_STAGE_NONE = 0;
    static constexpr uint32_t FAULT_STAGE_S1 = 1;
    static constexpr uint32_t FAULT_STAGE_S2 = 2;
    static constexpr uint32_t FAULT_ATTR_WRITE = 1u << 0;
    static constexpr uint32_t FAULT_ATTR_STALL = 1u << 1;
    static constexpr uint32_t FAULT_ATTR_TERMINATE = 1u << 2;
    static constexpr uint32_t EVENT_STAG_MASK = 0xffff;
    static constexpr uint32_t EVENT_STALL = 1u << 31;
    static constexpr uint32_t EVENT_PNU_SHIFT = 33;
    static constexpr uint32_t EVENT_IND_SHIFT = 34;
    static constexpr uint32_t EVENT_RNW_SHIFT = 35;
    static constexpr uint32_t EVENT_NSIPA_SHIFT = 38;
    static constexpr uint32_t EVENT_S2_SHIFT = 39;
    static constexpr uint32_t EVENT_CLASS_SHIFT = 40;
    static constexpr uint32_t EVENT_CLASS_MASK = 0x3;
    static constexpr uint32_t EVENT_GPCF_SHIFT = 16;
    static constexpr uint32_t EVENT_CLASS_CD = 0;
    static constexpr uint32_t EVENT_CLASS_TT = 1;
    static constexpr uint32_t EVENT_CLASS_IN = 2;
    static constexpr uint32_t EVENT_SYNTHETIC = 0x01;
    static constexpr uint32_t EVENT_F_UUT = 0x01;
    static constexpr uint32_t EVENT_C_BAD_STREAMID = 0x02;
    static constexpr uint32_t EVENT_F_STE_FETCH = 0x03;
    static constexpr uint32_t EVENT_C_BAD_STE = 0x04;
    static constexpr uint32_t EVENT_F_BAD_ATS_TREQ = 0x05;
    static constexpr uint32_t EVENT_F_STREAM_DISABLED = 0x06;
    static constexpr uint32_t EVENT_F_TRANSL_FORBIDDEN = 0x07;
    static constexpr uint32_t EVENT_C_BAD_SUBSTREAMID = 0x08;
    static constexpr uint32_t EVENT_F_CD_FETCH = 0x09;
    static constexpr uint32_t EVENT_C_BAD_CD = 0x0a;
    static constexpr uint32_t EVENT_F_WALK_EABT = 0x0b;
    static constexpr uint32_t EVENT_F_TRANSLATION = 0x10;
    static constexpr uint32_t EVENT_F_ADDR_SIZE = 0x11;
    static constexpr uint32_t EVENT_F_ACCESS = 0x12;
    static constexpr uint32_t EVENT_F_PERMISSION = 0x13;
    static constexpr uint32_t EVENT_F_TLB_CONFLICT = 0x20;
    static constexpr uint32_t EVENT_F_CFG_CONFLICT = 0x21;
    static constexpr uint32_t EVENT_F_VMS_FETCH = 0x25;
    static constexpr uint32_t EVENT_SSID_MASK = 0xfffff;
    static constexpr uint64_t EVENT_IPA_MASK = 0x000ffffffffff000ULL;
    static constexpr uint64_t EVENT_FETCH_ADDR_MASK = 0x000ffffffffff8ULL;
    static constexpr uint64_t EVENT_CONFLICT_REASON_TLB_TAG_MISMATCH = 0x544c4201ULL;
    static constexpr uint64_t EVENT_CONFLICT_REASON_CFG_STE_CONT = 0x43464701ULL;
    static constexpr uint32_t ENDPOINT_REPLAY_BLOCKING_ENABLE = 0x1;
    static constexpr uint32_t STALL_RECORD_CAPACITY = 32;
    static constexpr uint32_t ENDPOINT_REPLAY_CAPACITY = 32;

    using stall_record_table = std::array<stall_record, STALL_RECORD_CAPACITY>;
    using endpoint_replay_table =
        std::array<endpoint_replay_record, ENDPOINT_REPLAY_CAPACITY>;

    struct event_record_layout {
        uint32_t event_number = EVENT_SYNTHETIC;
        uint32_t detail = 0;
        uint64_t detail64 = 0;
        std::array<uint64_t, 4> words {};
        bool smmu_disabled_recordable = false;
    };

    apollo_smmu_arch_core() = default;

    const ownership_contract& ownership() const
    {
        return m_ownership;
    }

    const state_ownership_contract& state_ownership() const
    {
        return m_state_ownership;
    }

    bool owns_translation_state() const
    {
        return m_ownership.owner == translation_owner::apollo_systemc &&
               !m_ownership.qemu_translation_bridge_enabled;
    }

    bool preserves_compatibility_adapter() const
    {
        return m_ownership.compatibility_adapter_required;
    }

    bool owns_register_queue_state() const
    {
        return owns_translation_state() && m_state_ownership.register_queue_state;
    }

    bool owns_stream_context_descriptor_state() const
    {
        return owns_translation_state() && m_state_ownership.stream_context_descriptor_state;
    }

    bool owns_page_table_walker_state() const
    {
        return owns_translation_state() && m_state_ownership.page_table_walker_state;
    }

    bool owns_fault_replay_state() const
    {
        return owns_translation_state() && m_state_ownership.fault_replay_state;
    }

    register_aperture classify_register(uint64_t addr) const
    {
        if (addr >= SMMUV3_APERTURE_BASE &&
            addr < SMMUV3_APERTURE_BASE + SMMUV3_APERTURE_SIZE) {
            return register_aperture::smmuv3;
        }

        if (preserves_compatibility_adapter() &&
            addr >= COMPATIBILITY_APERTURE_BASE &&
            addr < SMMUV3_APERTURE_BASE) {
            return register_aperture::compatibility;
        }

        return register_aperture::invalid;
    }

    static constexpr uint32_t queue_entries(uint64_t queue_base)
    {
        const uint32_t log2_entries = static_cast<uint32_t>(queue_base & 0x1f);

        if (log2_entries < 1 || log2_entries > 15) {
            return 0;
        }

        return 1u << log2_entries;
    }

    static constexpr uint64_t queue_base_addr(uint64_t queue_base)
    {
        return queue_base & QUEUE_BASE_MASK;
    }

    static constexpr uint32_t queue_index(uint32_t value, uint32_t entries)
    {
        return entries == 0 ? 0 : value & (entries - 1);
    }

    static constexpr uint64_t walker_legacy_level_index(uint64_t iova, uint32_t level)
    {
        return (iova >> (WALKER_L0_SHIFT - level * WALKER_LEVEL_BITS)) &
               WALKER_INDEX_MASK;
    }

    static constexpr uint32_t walker_page_shift(uint32_t granule)
    {
        switch (granule) {
        case WALKER_GRANULE_16K:
            return 14;
        case WALKER_GRANULE_64K:
            return 16;
        case WALKER_GRANULE_4K:
        default:
            return 12;
        }
    }

    static constexpr uint32_t walker_index_bits(uint32_t granule)
    {
        return walker_page_shift(granule) - 3;
    }

    static constexpr uint64_t walker_granule_size(uint32_t granule)
    {
        return 1ULL << walker_page_shift(granule);
    }

    static constexpr uint64_t walker_granule_mask(uint32_t granule)
    {
        return walker_granule_size(granule) - 1;
    }

    static constexpr bool walker_granule_supported(uint32_t granule)
    {
        return granule == WALKER_GRANULE_4K || granule == WALKER_GRANULE_16K ||
               granule == WALKER_GRANULE_64K;
    }

    static constexpr uint64_t walker_output_mask(uint32_t granule)
    {
        return WALKER_DESC_OUTPUT_MASK & ~walker_granule_mask(granule);
    }

    static constexpr uint32_t walker_levels(uint32_t granule)
    {
        return granule == WALKER_GRANULE_64K ? 3 : WALKER_LEVELS;
    }

    static constexpr uint32_t walker_covered_bits(uint32_t granule,
                                                  uint32_t start_level,
                                                  uint32_t levels)
    {
        const uint32_t pages = walker_page_shift(granule);
        const uint32_t depth = levels > start_level ? levels - start_level : 0;

        return pages + depth * walker_index_bits(granule);
    }

    static constexpr uint64_t walker_level_index(uint64_t iova, uint32_t granule,
                                                 uint32_t levels, uint32_t level)
    {
        const uint32_t index_bits = walker_index_bits(granule);
        const uint32_t page_shift = walker_page_shift(granule);
        const uint32_t remaining = levels - level - 1;
        const uint32_t shift = page_shift + remaining * index_bits;
        const uint64_t mask = (1ULL << index_bits) - 1;

        return (iova >> shift) & mask;
    }

    static constexpr uint64_t walker_level_offset_mask(uint32_t granule,
                                                       uint32_t levels,
                                                       uint32_t level)
    {
        const uint32_t index_bits = walker_index_bits(granule);
        const uint32_t page_shift = walker_page_shift(granule);
        const uint32_t remaining = levels - level - 1;
        const uint32_t shift = page_shift + remaining * index_bits;

        return shift >= 64 ? std::numeric_limits<uint64_t>::max() :
                             ((1ULL << shift) - 1);
    }

    static constexpr uint32_t stream_table_log2size(uint32_t cfg)
    {
        return cfg & STRTAB_CFG_LOG2SIZE_MASK;
    }

    static constexpr uint32_t stream_table_split(uint32_t cfg)
    {
        return (cfg >> STRTAB_CFG_SPLIT_SHIFT) & STRTAB_CFG_SPLIT_MASK;
    }

    static constexpr uint32_t stream_table_format(uint32_t cfg)
    {
        return (cfg >> STRTAB_CFG_FMT_SHIFT) & STRTAB_CFG_FMT_MASK;
    }

    static constexpr bool stream_id_in_bounds(uint32_t cfg, uint32_t stream_id)
    {
        const uint32_t log2size = stream_table_log2size(cfg);

        return log2size >= 32 ? true : stream_id < (1u << log2size);
    }

    static constexpr uint64_t stream_table_base_addr(uint64_t base)
    {
        return base & STRTAB_BASE_ADDR_MASK;
    }

    static constexpr uint32_t stream_table_l1_index(uint32_t stream_id,
                                                    uint32_t split)
    {
        return stream_id >> split;
    }

    static constexpr uint32_t stream_table_l2_index(uint32_t stream_id,
                                                    uint32_t split)
    {
        return stream_id & ((1u << split) - 1);
    }

    static constexpr uint32_t stream_table_l1_span(uint64_t desc)
    {
        return static_cast<uint32_t>(desc & STRTAB_L1_DESC_SPAN_MASK);
    }

    static constexpr uint64_t stream_table_l1_l2_base(uint64_t desc)
    {
        return desc & STRTAB_L1_DESC_L2PTR_MASK;
    }

    static constexpr bool stream_table_split_valid(uint32_t cfg)
    {
        const uint32_t split = stream_table_split(cfg);

        return split != 0 && split < 32 && split <= stream_table_log2size(cfg);
    }

    static constexpr bool stream_table_l1_desc_valid(uint64_t desc,
                                                     uint32_t split,
                                                     uint32_t l2_index)
    {
        const uint32_t span = stream_table_l1_span(desc);
        const uint32_t covered_bits = span == 0 ? 0 : span - 1;
        const uint64_t l2_base = stream_table_l1_l2_base(desc);

        return l2_base != 0 && covered_bits >= split &&
               l2_index < (1u << covered_bits);
    }

    static constexpr uint32_t ste_config(uint64_t ste0)
    {
        return static_cast<uint32_t>((ste0 >> STE_CFG_SHIFT) & STE_CFG_MASK);
    }

    static constexpr bool ste_config_supported(uint64_t ste0)
    {
        const uint32_t cfg = ste_config(ste0);

        return cfg == 0 || cfg == 1 || cfg == STE_CFG_BYPASS ||
               cfg == STE_CFG_S1_TRANS || cfg == STE_CFG_S2_TRANS ||
               cfg == STE_CFG_NESTED;
    }

    static constexpr bool ste_is_s1_enabled(uint64_t ste0)
    {
        return ((ste0 & STE_VALID) != 0 && ste_config(ste0) == STE_CFG_S1_TRANS) ||
               ((ste0 & (STE_VALID | STE_S1_ENABLED)) ==
                (STE_VALID | STE_S1_ENABLED));
    }

    static constexpr uint32_t ste_s1fmt(uint64_t ste0)
    {
        return static_cast<uint32_t>((ste0 >> STE_S1FMT_SHIFT) & STE_S1FMT_MASK);
    }

    static constexpr uint32_t ste_s1cdmax(uint64_t ste0)
    {
        return static_cast<uint32_t>((ste0 >> STE_S1CDMAX_SHIFT) &
                                     STE_S1CDMAX_MASK);
    }

    static constexpr uint32_t ste_s1dss(uint64_t ste1)
    {
        return static_cast<uint32_t>(ste1 & STE_S1DSS_MASK);
    }

    static constexpr uint32_t ste_eats(uint64_t ste1)
    {
        return static_cast<uint32_t>((ste1 >> STE_EATS_SHIFT) & STE_EATS_MASK);
    }

    static constexpr bool ste_s1mpam(uint64_t ste1)
    {
        return (ste1 & STE_S1MPAM) != 0;
    }

    static constexpr uint64_t ste_vmsptr(uint64_t ste5)
    {
        return ste5 & STE_VMSPTR_MASK;
    }

    static constexpr uint16_t ste_partid(uint64_t ste4)
    {
        return static_cast<uint16_t>((ste4 >> STE_PARTID_SHIFT) &
                                     STE_PARTID_MASK);
    }

    static constexpr uint8_t ste_pmg(uint64_t ste5)
    {
        return static_cast<uint8_t>((ste5 >> STE_PMG_SHIFT) & STE_PMG_MASK);
    }

    static constexpr uint32_t effective_eats(uint64_t ste0, uint64_t ste1,
                                             bool atschk_enabled)
    {
        const uint32_t cfg = ste_config(ste0);
        const uint32_t eats = ste_eats(ste1);

        if ((ste0 & STE_VALID) == 0 || cfg == 0 || cfg == STE_CFG_BYPASS) {
            return STE_EATS_DISABLED;
        }
        if ((eats == STE_EATS_SPLIT || eats == STE_EATS_DPT) && !atschk_enabled) {
            return STE_EATS_DISABLED;
        }
        return eats;
    }

    static constexpr bool cd_is_valid(uint64_t cd0)
    {
        return (cd0 & CD_VALID) != 0 || (cd0 & CD_VALID_ARCHITECTED) != 0;
    }

    static constexpr uint64_t cd_ttbr(uint64_t cd1)
    {
        const uint64_t architected = cd1 & CD_TTB0_MASK;

        return architected != 0 ? architected : (cd1 & WALKER_DESC_OUTPUT_MASK);
    }

    static constexpr uint16_t cd_asid(uint64_t cd0)
    {
        return static_cast<uint16_t>((cd0 & CD_ASID_MASK) >> CD_ASID_SHIFT);
    }

    static constexpr uint16_t cd_partid(uint64_t cd5)
    {
        return static_cast<uint16_t>((cd5 >> CD_PARTID_SHIFT) & CD_PARTID_MASK);
    }

    static constexpr uint8_t cd_pmg(uint64_t cd5)
    {
        return static_cast<uint8_t>((cd5 >> CD_PMG_SHIFT) & CD_PMG_MASK);
    }

    static constexpr bool cd_index_valid(uint32_t ssid, uint32_t s1cdmax)
    {
        return s1cdmax >= 32 ? true : ssid < (1u << s1cdmax);
    }

    static constexpr uint32_t fault_class(uint32_t reason)
    {
        switch (reason) {
        case FAULT_STE_FETCH:
        case FAULT_STE_INVALID:
        case FAULT_BAD_STREAM_ID:
        case FAULT_VMS_FETCH:
            return FAULT_CLASS_STE;
        case FAULT_CD_FETCH:
        case FAULT_CD_INVALID:
        case FAULT_STREAM_DISABLED:
        case FAULT_BAD_SUBSTREAMID:
            return FAULT_CLASS_CD;
        case FAULT_TABLE_INVALID:
        case FAULT_PAGE_INVALID:
        case FAULT_NEGATIVE_UNEXPECTED_PASS:
        case FAULT_ACCESS:
        case FAULT_PERMISSION:
        case FAULT_ADDR_SIZE:
        case FAULT_GRANULE:
        case FAULT_STAGE2:
        case FAULT_WALK_EABT:
        case FAULT_TRANSL_FORBIDDEN:
        case FAULT_TLB_CONFLICT:
        case FAULT_CFG_CONFLICT:
            return FAULT_CLASS_TRANSLATION;
        case FAULT_UNSUPPORTED_UPSTREAM:
        default:
            return FAULT_CLASS_NONE;
        }
    }

    static constexpr uint32_t fault_detail_word(uint32_t reason, uint32_t stage,
                                                uint32_t replay_id, bool stall,
                                                bool write)
    {
        uint32_t attrs = FAULT_ATTR_TERMINATE;

        if (stall) {
            attrs = FAULT_ATTR_STALL;
        }
        if (write) {
            attrs |= FAULT_ATTR_WRITE;
        }

        return (reason & 0xffu) | ((fault_class(reason) & 0xffu) << 8) |
               ((stage & 0xfu) << 16) | ((attrs & 0xffu) << 20) |
               ((replay_id & 0xffu) << 28);
    }

    void set_fetch_fault(uint32_t reason, uint32_t stage, uint64_t fetch_addr,
                         bool gpcf = false)
    {
        m_fault_replay_state.fault_reason = reason;
        m_fault_replay_state.fault_stage = stage;
        m_walker_state.last_fetch_addr = fetch_addr;
        m_fault_replay_state.fault_gpcf = gpcf;
    }

    bool begin_descriptor_walk(uint64_t iova, uint64_t table_pa,
                               const descriptor_walk_config& cfg)
    {
        m_walker_state.iova = iova;
        m_walker_state.ttbr = table_pa;
        m_walker_state.last_desc = 0;
        m_walker_state.last_pa = 0;
        m_walker_state.walk_depth = 0;
        m_walker_state.last_stage = cfg.stage;
        m_fault_replay_state.fault_stage = cfg.stage;
        m_fault_replay_state.fault_reason = FAULT_NONE;

        if (!walker_granule_supported(cfg.granule) || cfg.start_level >= cfg.levels) {
            m_fault_replay_state.fault_reason = FAULT_GRANULE;
            return false;
        }

        const uint32_t covered_bits =
            walker_covered_bits(cfg.granule, cfg.start_level, cfg.levels);
        if (covered_bits < 64 && (iova >> covered_bits) != 0) {
            m_fault_replay_state.fault_reason = FAULT_ADDR_SIZE;
            return false;
        }

        if (table_pa == 0) {
            m_fault_replay_state.fault_reason = FAULT_TABLE_INVALID;
            return false;
        }

        return true;
    }

    descriptor_fetch descriptor_fetch_address(uint64_t table_pa, uint64_t iova,
                                              const descriptor_walk_config& cfg,
                                              uint32_t level) const
    {
        descriptor_fetch fetch {};

        fetch.table_pa = table_pa;
        fetch.index = walker_level_index(iova, cfg.granule, cfg.levels, level);
        fetch.desc_pa = table_pa + fetch.index * sizeof(uint64_t);
        return fetch;
    }

    descriptor_fetch begin_descriptor_fetch(uint64_t table_pa, uint64_t iova,
                                            const descriptor_walk_config& cfg,
                                            uint32_t level)
    {
        descriptor_fetch fetch = descriptor_fetch_address(table_pa, iova, cfg, level);

        m_walker_state.last_fetch_addr = fetch.desc_pa;
        m_fault_replay_state.fault_stage = cfg.stage;
        return fetch;
    }

    void complete_descriptor_fetch(uint32_t stage, uint64_t fetch_pa,
                                   uint64_t desc)
    {
        m_walker_state.last_fetch_addr = fetch_pa;
        m_walker_state.last_desc = desc;
        m_fault_replay_state.fault_stage = stage;
        m_fault_replay_state.fault_reason = FAULT_NONE;
    }

    void fail_descriptor_fetch(uint32_t stage, uint64_t fetch_pa,
                               bool gpcf = false)
    {
        set_fetch_fault(FAULT_WALK_EABT, stage, fetch_pa, gpcf);
    }

    descriptor_memory_read begin_descriptor_memory_read(
        const descriptor_fetch& fetch, uint64_t transaction_pa,
        const descriptor_walk_config& cfg, bool stage2_translated)
    {
        descriptor_memory_read read {};

        read.desc_pa = fetch.desc_pa;
        read.transaction_pa = transaction_pa;
        read.stage = cfg.stage;
        read.stage2_translated = stage2_translated;
        m_walker_state.last_fetch_addr = transaction_pa;
        m_fault_replay_state.fault_stage = cfg.stage;
        return read;
    }

    void complete_descriptor_memory_read(const descriptor_memory_read& read,
                                         uint64_t desc)
    {
        complete_descriptor_fetch(read.stage, read.transaction_pa, desc);
    }

    void fail_descriptor_memory_read(const descriptor_memory_read& read,
                                     bool gpcf = false)
    {
        fail_descriptor_fetch(read.stage, read.transaction_pa, gpcf);
    }

    descriptor_step evaluate_descriptor_step(uint64_t iova, uint64_t desc_pa,
                                             uint64_t desc,
                                             const descriptor_walk_config& cfg,
                                             uint32_t level, bool write)
    {
        m_walker_state.last_desc = desc;
        m_fault_replay_state.fault_stage = cfg.stage;
        m_fault_replay_state.fault_reason = FAULT_NONE;

        if (level + 1 < cfg.levels) {
            if ((desc & WALKER_DESC_TYPE_MASK) == WALKER_DESC_TABLE) {
                descriptor_step step {};

                step.kind = descriptor_step_kind::table;
                step.next_table_pa = desc & walker_output_mask(cfg.granule);
                return step;
            }

            if ((desc & WALKER_DESC_TYPE_MASK) == WALKER_DESC_BLOCK) {
                return finish_descriptor_leaf(iova, desc, cfg, level, write,
                                              true);
            }

            m_fault_replay_state.fault_reason = FAULT_TABLE_INVALID;
            return descriptor_fault(FAULT_TABLE_INVALID);
        }

        if ((desc & WALKER_DESC_TYPE_MASK) != WALKER_DESC_PAGE) {
            m_fault_replay_state.fault_reason = FAULT_PAGE_INVALID;
            return descriptor_fault(FAULT_PAGE_INVALID);
        }

        return finish_descriptor_leaf(iova, desc, cfg, level, write, false);
    }

    descriptor_step descriptor_fault(uint32_t reason)
    {
        descriptor_step step {};

        step.fault_reason = reason;
        m_fault_replay_state.fault_reason = reason;
        return step;
    }

    descriptor_step finish_descriptor_leaf(uint64_t iova, uint64_t desc,
                                           const descriptor_walk_config& cfg,
                                           uint32_t level, bool write,
                                           bool block_leaf)
    {
        if ((desc & WALKER_DESC_AF) == 0) {
            return descriptor_fault(FAULT_ACCESS);
        }
        if (write && (desc & WALKER_DESC_AP_RO) != 0) {
            return descriptor_fault(FAULT_PERMISSION);
        }

        const uint64_t offset_mask =
            walker_level_offset_mask(cfg.granule, cfg.levels, level);
        const uint64_t output_mask = 0x0000ffffffffffffULL & ~offset_mask;
        descriptor_step step {};

        step.kind = descriptor_step_kind::leaf;
        step.pa = (desc & output_mask) | (iova & offset_mask);
        step.fault_reason = FAULT_NONE;
        step.block_leaf = block_leaf;
        step.block_nt = block_leaf && ((desc & WALKER_DESC_NT) != 0);
        m_walker_state.last_pa = step.pa;
        m_walker_state.walk_depth = cfg.levels - cfg.start_level;
        m_walker_state.last_stage = cfg.stage;
        return step;
    }

    static constexpr uint32_t event_class_for_fault(uint32_t reason,
                                                    uint32_t fault_stage,
                                                    uint32_t configured_class)
    {
        if (fault_stage == FAULT_STAGE_S2) {
            return configured_class & EVENT_CLASS_MASK;
        }

        switch (reason) {
        case FAULT_TABLE_INVALID:
        case FAULT_WALK_EABT:
            return EVENT_CLASS_TT;
        default:
            return EVENT_CLASS_IN;
        }
    }

    static constexpr uint32_t event_number_for_fault(uint32_t reason)
    {
        switch (reason) {
        case FAULT_BAD_STREAM_ID:
            return EVENT_C_BAD_STREAMID;
        case FAULT_UNSUPPORTED_UPSTREAM:
            return EVENT_F_UUT;
        case FAULT_STE_FETCH:
            return EVENT_F_STE_FETCH;
        case FAULT_STE_INVALID:
            return EVENT_C_BAD_STE;
        case FAULT_CD_FETCH:
            return EVENT_F_CD_FETCH;
        case FAULT_CD_INVALID:
            return EVENT_C_BAD_CD;
        case FAULT_STREAM_DISABLED:
            return EVENT_F_STREAM_DISABLED;
        case FAULT_BAD_SUBSTREAMID:
            return EVENT_C_BAD_SUBSTREAMID;
        case FAULT_ADDR_SIZE:
            return EVENT_F_ADDR_SIZE;
        case FAULT_ACCESS:
            return EVENT_F_ACCESS;
        case FAULT_PERMISSION:
            return EVENT_F_PERMISSION;
        case FAULT_BAD_ATS_TREQ:
            return EVENT_F_BAD_ATS_TREQ;
        case FAULT_TRANSL_FORBIDDEN:
            return EVENT_F_TRANSL_FORBIDDEN;
        case FAULT_TLB_CONFLICT:
            return EVENT_F_TLB_CONFLICT;
        case FAULT_CFG_CONFLICT:
            return EVENT_F_CFG_CONFLICT;
        case FAULT_WALK_EABT:
            return EVENT_F_WALK_EABT;
        case FAULT_VMS_FETCH:
            return EVENT_F_VMS_FETCH;
        case FAULT_TABLE_INVALID:
        case FAULT_PAGE_INVALID:
        case FAULT_NEGATIVE_UNEXPECTED_PASS:
        case FAULT_GRANULE:
        case FAULT_STAGE2:
            return EVENT_F_TRANSLATION;
        default:
            return EVENT_SYNTHETIC;
        }
    }

    static constexpr uint64_t event_record_word0(uint32_t stream_id,
                                                 uint32_t event_number,
                                                 bool ssid_valid,
                                                 uint32_t ssid)
    {
        return (static_cast<uint64_t>(stream_id) << 32) |
               (static_cast<uint64_t>(ssid & EVENT_SSID_MASK) << 12) |
               (ssid_valid ? (1ULL << 11) : 0ULL) | (event_number & 0xffu);
    }

    static constexpr bool event_record_has_res0_payload(uint32_t event_number)
    {
        switch (event_number) {
        case EVENT_C_BAD_STREAMID:
        case EVENT_C_BAD_STE:
        case EVENT_C_BAD_CD:
        case EVENT_F_STREAM_DISABLED:
            return true;
        default:
            return false;
        }
    }

    static constexpr bool event_record_has_fetch_reason(uint32_t event_number)
    {
        switch (event_number) {
        case EVENT_F_STE_FETCH:
        case EVENT_F_CD_FETCH:
        case EVENT_F_WALK_EABT:
        case EVENT_F_VMS_FETCH:
            return true;
        default:
            return false;
        }
    }

    static constexpr bool event_record_has_conflict_reason(uint32_t event_number)
    {
        return event_number == EVENT_F_TLB_CONFLICT ||
               event_number == EVENT_F_CFG_CONFLICT;
    }

    static constexpr uint64_t event_record_conflict_reason(uint32_t event_number)
    {
        switch (event_number) {
        case EVENT_F_TLB_CONFLICT:
            return EVENT_CONFLICT_REASON_TLB_TAG_MISMATCH;
        case EVENT_F_CFG_CONFLICT:
            return EVENT_CONFLICT_REASON_CFG_STE_CONT;
        default:
            return 0;
        }
    }

    uint64_t event_record_word1(uint32_t reason, uint16_t stag, bool stall,
                                bool write, bool privileged,
                                bool instruction) const
    {
        if (!stall) {
            return 0;
        }

        uint64_t word = static_cast<uint64_t>(stag & EVENT_STAG_MASK) |
                        static_cast<uint64_t>(EVENT_STALL);
        if (privileged) {
            word |= 1ULL << EVENT_PNU_SHIFT;
        }
        if (!write && instruction) {
            word |= 1ULL << EVENT_IND_SHIFT;
        }
        if (!write) {
            word |= 1ULL << EVENT_RNW_SHIFT;
        }
        if (m_fault_replay_state.fault_stage == FAULT_STAGE_S2) {
            word |= 1ULL << EVENT_S2_SHIFT;
            if (m_fault_replay_state.fault_nsipa) {
                word |= 1ULL << EVENT_NSIPA_SHIFT;
            }
        }
        word |= static_cast<uint64_t>(event_class_for_fault(
                    reason, m_fault_replay_state.fault_stage,
                    m_fault_replay_state.fault_event_class) &
                                      EVENT_CLASS_MASK)
                << EVENT_CLASS_SHIFT;
        if (m_fault_replay_state.fault_gpcf &&
            (reason == FAULT_STE_FETCH || reason == FAULT_CD_FETCH ||
             reason == FAULT_WALK_EABT || reason == FAULT_VMS_FETCH)) {
            word |= 1ULL << EVENT_GPCF_SHIFT;
        }
        return word;
    }

    uint64_t event_record_nonstall_word1(uint32_t reason,
                                         uint32_t event_number,
                                         uint64_t iova) const
    {
        if (event_number == EVENT_F_UUT && reason == FAULT_UNSUPPORTED_UPSTREAM) {
            return 0;
        }

        if (event_record_has_res0_payload(event_number)) {
            return 0;
        }

        if (event_record_has_fetch_reason(event_number)) {
            return m_fault_replay_state.fault_gpcf ? (1ULL << EVENT_GPCF_SHIFT) : 0;
        }

        return iova;
    }

    uint64_t event_record_word2(uint32_t reason, uint32_t event_number,
                                uint64_t len) const
    {
        if (event_number == EVENT_F_UUT && reason == FAULT_UNSUPPORTED_UPSTREAM) {
            return 0;
        }

        if (event_record_has_res0_payload(event_number)) {
            return 0;
        }

        if (event_number == EVENT_SYNTHETIC) {
            return len;
        }

        if (m_fault_replay_state.fault_stage == FAULT_STAGE_S2 ||
            m_walker_state.last_ipa != 0) {
            return m_walker_state.last_ipa;
        }

        return 0;
    }

    uint64_t event_record_word3(uint32_t event_number) const
    {
        switch (event_number) {
        case EVENT_F_TRANSLATION:
        case EVENT_F_ADDR_SIZE:
        case EVENT_F_ACCESS:
        case EVENT_F_PERMISSION:
            if (m_fault_replay_state.fault_stage == FAULT_STAGE_S2) {
                return m_walker_state.last_ipa & EVENT_IPA_MASK;
            }
            return 0;
        case EVENT_F_STE_FETCH:
        case EVENT_F_CD_FETCH:
        case EVENT_F_WALK_EABT:
        case EVENT_F_VMS_FETCH:
            return m_walker_state.last_fetch_addr & EVENT_FETCH_ADDR_MASK;
        case EVENT_F_TLB_CONFLICT:
        case EVENT_F_CFG_CONFLICT:
            return event_record_conflict_reason(event_number);
        default:
            return 0;
        }
    }

    event_record_layout build_event_record(uint32_t stream_id, uint32_t reason,
                                           uint64_t iova, uint64_t len,
                                           bool stall, bool write,
                                           bool ssid_valid, uint32_t ssid,
                                           uint16_t stag, bool privileged,
                                           bool instruction) const
    {
        event_record_layout layout {};

        layout.event_number = event_number_for_fault(reason);
        layout.detail = fault_detail_word(reason, m_fault_replay_state.fault_stage,
                                          m_fault_replay_state.next_fault_replay_id,
                                          stall, write);
        layout.detail64 =
            static_cast<uint64_t>(layout.detail) |
            (static_cast<uint64_t>(m_fault_replay_state.next_fault_replay_id & 0xffffu)
             << 32);
        layout.words = {{
            event_record_word0(stream_id, layout.event_number, ssid_valid, ssid),
            stall ? event_record_word1(reason, stag, stall, write, privileged,
                                       instruction) :
                    event_record_nonstall_word1(reason, layout.event_number, iova),
            stall ? iova : event_record_word2(reason, layout.event_number, len),
            event_record_word3(layout.event_number),
        }};
        layout.smmu_disabled_recordable =
            (layout.event_number == EVENT_F_UUT &&
             reason == FAULT_UNSUPPORTED_UPSTREAM) ||
            (layout.event_number == EVENT_F_TRANSL_FORBIDDEN &&
             reason == FAULT_TRANSL_FORBIDDEN);
        return layout;
    }

    uint32_t protocol_status() const
    {
        return ((m_fault_replay_state.pri_responses & 0xffffu) << 16) |
               (m_fault_replay_state.ats_responses & 0xffffu);
    }

    uint32_t ats_detail() const
    {
        const uint32_t total = m_fault_replay_state.ats_success +
                               m_fault_replay_state.ats_ur +
                               m_fault_replay_state.ats_ca;

        return (m_fault_replay_state.ats_success & 0xffu) |
               ((m_fault_replay_state.ats_ur & 0xffu) << 8) |
               ((m_fault_replay_state.ats_ca & 0xffu) << 16) |
               ((total & 0xffu) << 24);
    }

    uint32_t pri_detail(uint32_t pending_count) const
    {
        return (pending_count & 0xffu) |
               ((m_fault_replay_state.pri_responses & 0xffu) << 8) |
               ((m_fault_replay_state.pri_rejected & 0xffu) << 16) |
               ((m_fault_replay_state.pri_unknown & 0xffu) << 24);
    }

    uint32_t stall_status() const
    {
        return (m_fault_replay_state.stall_pending & 0xffu) |
               ((m_fault_replay_state.stall_retried & 0xffu) << 8) |
               ((m_fault_replay_state.stall_terminated & 0xffu) << 16) |
               ((m_fault_replay_state.resume_unknown & 0xffu) << 24);
    }

    uint32_t stall_merge_status() const
    {
        return (m_fault_replay_state.stall_suppressed & 0xffffu) |
               ((m_fault_replay_state.stall_merged & 0xffffu) << 16);
    }

    uint32_t endpoint_replay_status() const
    {
        return (m_fault_replay_state.endpoint_replay_pending & 0xffu) |
               ((m_fault_replay_state.endpoint_replay_retried & 0xffu) << 8) |
               ((m_fault_replay_state.endpoint_replay_succeeded & 0xffu) << 16) |
               ((m_fault_replay_state.endpoint_replay_terminated & 0xffu) << 24);
    }

    uint32_t endpoint_replay_ctrl() const
    {
        return m_fault_replay_state.endpoint_blocking_enabled ?
                   ENDPOINT_REPLAY_BLOCKING_ENABLE :
                   0;
    }

    uint32_t endpoint_block_status() const
    {
        return (m_fault_replay_state.endpoint_block_waits & 0xffu) |
               ((m_fault_replay_state.endpoint_block_resumed & 0xffu) << 8) |
               ((m_fault_replay_state.endpoint_block_failed & 0xffu) << 16);
    }

    uint32_t early_retry_status() const
    {
        return (m_fault_replay_state.early_retry_attempted & 0xffu) |
               ((m_fault_replay_state.early_retry_succeeded & 0xffu) << 8) |
               ((m_fault_replay_state.early_retry_failed & 0xffu) << 16) |
               ((m_fault_replay_state.early_retry_discarded & 0xffu) << 24);
    }

    stall_record_table& stall_records()
    {
        return m_stall_records;
    }

    const stall_record_table& stall_records() const
    {
        return m_stall_records;
    }

    bool stag_pending(uint16_t stag) const
    {
        if (stag == 0) {
            return false;
        }
        for (const auto& stall : m_stall_records) {
            if (stall.pending && stall.stag == stag) {
                return true;
            }
        }
        return false;
    }

    stall_record* find_stall(uint32_t stream_id, uint16_t stag)
    {
        if (stag == 0) {
            return nullptr;
        }
        for (auto& stall : m_stall_records) {
            if (stall.pending && stall.stream_id == stream_id && stall.stag == stag) {
                return &stall;
            }
        }
        return nullptr;
    }

    const stall_record* find_stall(uint32_t stream_id, uint16_t stag) const
    {
        if (stag == 0) {
            return nullptr;
        }
        for (const auto& stall : m_stall_records) {
            if (stall.pending && stall.stream_id == stream_id && stall.stag == stag) {
                return &stall;
            }
        }
        return nullptr;
    }

    stall_record* find_stall_by_fault(uint32_t stream_id, uint64_t iova,
                                      bool ssid_valid, uint32_t ssid)
    {
        for (auto& stall : m_stall_records) {
            if (!stall.pending || stall.stream_id != stream_id || stall.iova != iova ||
                stall.ssid_valid != ssid_valid) {
                continue;
            }
            if (ssid_valid && stall.ssid != ssid) {
                continue;
            }
            return &stall;
        }
        return nullptr;
    }

    void reset_stall_records()
    {
        m_stall_records = {};
    }

    endpoint_replay_table& endpoint_replay_records()
    {
        return m_endpoint_replay_records;
    }

    const endpoint_replay_table& endpoint_replay_records() const
    {
        return m_endpoint_replay_records;
    }

    endpoint_replay_record* find_pending_endpoint_replay(uint32_t stream_id,
                                                         uint16_t stag)
    {
        if (stag == 0) {
            return nullptr;
        }
        for (auto& replay : m_endpoint_replay_records) {
            if (replay.pending && replay.stream_id == stream_id && replay.stag == stag) {
                return &replay;
            }
        }
        return nullptr;
    }

    const endpoint_replay_record* find_pending_endpoint_replay(uint32_t stream_id,
                                                               uint16_t stag) const
    {
        if (stag == 0) {
            return nullptr;
        }
        for (const auto& replay : m_endpoint_replay_records) {
            if (replay.pending && replay.stream_id == stream_id && replay.stag == stag) {
                return &replay;
            }
        }
        return nullptr;
    }

    endpoint_replay_record* find_endpoint_replay(uint32_t stream_id, uint16_t stag)
    {
        if (stag == 0) {
            return nullptr;
        }
        for (auto& replay : m_endpoint_replay_records) {
            if (replay.stream_id == stream_id && replay.stag == stag) {
                return &replay;
            }
        }
        return nullptr;
    }

    endpoint_replay_allocation allocate_endpoint_replay_record(
        uint32_t stream_id, uint16_t stag, uint64_t iova, uint64_t len,
        bool write, bool ssid_valid, uint32_t ssid,
        const uint8_t* payload = nullptr, std::size_t payload_len = 0)
    {
        endpoint_replay_allocation allocation {};

        if (stag == 0) {
            allocation.invalid_stag = true;
            return allocation;
        }

        if (auto* const existing = find_pending_endpoint_replay(stream_id, stag)) {
            allocation.record = existing;
            allocation.duplicate = true;
            return allocation;
        }

        for (auto& replay : m_endpoint_replay_records) {
            if (replay.pending) {
                continue;
            }

            replay = endpoint_replay_record {};
            replay.stream_id = stream_id;
            replay.stag = stag;
            replay.iova = iova;
            replay.len = len;
            replay.ssid = ssid;
            replay.ssid_valid = ssid_valid;
            replay.write = write;
            replay.pending = true;
            if (write && payload != nullptr && payload_len != 0) {
                replay.payload.assign(payload, payload + payload_len);
            }
            m_fault_replay_state.endpoint_replay_pending++;

            allocation.record = &replay;
            allocation.allocated = true;
            return allocation;
        }

        m_fault_replay_state.resume_unknown++;
        allocation.capacity_exhausted = true;
        return allocation;
    }

    bool retire_endpoint_replay(endpoint_replay_record& replay, bool retry_response,
                                bool redrive_success)
    {
        if (!replay.pending) {
            return false;
        }

        replay.replayed = true;
        m_fault_replay_state.endpoint_replay_pending =
            m_fault_replay_state.endpoint_replay_pending == 0 ?
                0 :
                m_fault_replay_state.endpoint_replay_pending - 1;

        if (retry_response) {
            m_fault_replay_state.endpoint_replay_retried++;
            if (replay.early_retry_succeeded) {
                replay.replay_status = tlm::TLM_OK_RESPONSE;
                replay.succeeded = true;
            } else {
                replay.succeeded = redrive_success;
            }
            if (replay.succeeded) {
                m_fault_replay_state.endpoint_replay_succeeded++;
            } else {
                m_fault_replay_state.endpoint_replay_failed++;
            }
            if (replay.redriven) {
                m_fault_replay_state.endpoint_replay_redriven++;
            }
        } else {
            m_fault_replay_state.endpoint_replay_terminated++;
        }

        replay.pending = false;
        return true;
    }

    bool begin_endpoint_replay_redrive(endpoint_replay_record& replay)
    {
        replay.replay_pa = 0;
        replay.replay_len = 0;
        replay.redriven = false;
        replay.succeeded = false;
        replay.replay_status = tlm::TLM_INCOMPLETE_RESPONSE;

        if (replay.len > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
            fail_endpoint_replay_redrive(replay, tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }

        const auto replay_len = static_cast<std::size_t>(replay.len);
        if (replay.write) {
            if (replay.payload.size() < replay_len) {
                fail_endpoint_replay_redrive(replay, tlm::TLM_BURST_ERROR_RESPONSE);
                return false;
            }
        } else {
            replay.payload.assign(replay_len, 0);
        }

        return true;
    }

    endpoint_replay_segment prepare_endpoint_replay_segment(endpoint_replay_record& replay,
                                                            uint64_t offset,
                                                            uint64_t pa,
                                                            uint64_t segment_len)
    {
        endpoint_replay_segment segment {};

        segment.offset = offset;
        segment.pa = pa;
        segment.len = segment_len;
        segment.first_segment = offset == 0;

        if (segment_len == 0 || offset > replay.len ||
            segment_len > replay.len - offset ||
            segment_len > static_cast<uint64_t>(std::numeric_limits<unsigned int>::max())) {
            fail_endpoint_replay_redrive(replay, tlm::TLM_BURST_ERROR_RESPONSE);
            return segment;
        }
        if (offset > static_cast<uint64_t>(replay.payload.size()) ||
            segment_len > static_cast<uint64_t>(replay.payload.size()) - offset) {
            fail_endpoint_replay_redrive(replay, tlm::TLM_BURST_ERROR_RESPONSE);
            return segment;
        }

        if (segment.first_segment) {
            replay.replay_pa = pa;
        }
        segment.payload = replay.payload.data() + static_cast<std::size_t>(offset);
        segment.valid = true;
        return segment;
    }

    bool complete_endpoint_replay_segment(endpoint_replay_record& replay,
                                          const endpoint_replay_segment& segment,
                                          tlm::tlm_response_status status)
    {
        replay.replay_status = status;
        if (!segment.valid || status != tlm::TLM_OK_RESPONSE) {
            return false;
        }

        replay.replay_len = segment.offset + segment.len;
        return true;
    }

    endpoint_replay_transaction begin_endpoint_replay_transaction(
        endpoint_replay_record& replay,
        const endpoint_replay_segment& segment)
    {
        endpoint_replay_transaction transaction {};

        if (!segment.valid || segment.payload == nullptr || segment.len == 0 ||
            segment.len > static_cast<uint64_t>(std::numeric_limits<unsigned int>::max())) {
            fail_endpoint_replay_redrive(replay, tlm::TLM_BURST_ERROR_RESPONSE);
            return transaction;
        }

        transaction.pa = segment.pa;
        transaction.len = segment.len;
        transaction.payload = segment.payload;
        transaction.write = replay.write;
        transaction.valid = true;
        replay.replay_status = tlm::TLM_INCOMPLETE_RESPONSE;
        return transaction;
    }

    bool complete_endpoint_replay_transaction(endpoint_replay_record& replay,
                                              const endpoint_replay_segment& segment,
                                              const endpoint_replay_transaction& transaction,
                                              tlm::tlm_response_status status)
    {
        if (!transaction.valid || !segment.valid || transaction.pa != segment.pa ||
            transaction.len != segment.len || transaction.payload != segment.payload) {
            fail_endpoint_replay_redrive(replay, tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }

        return complete_endpoint_replay_segment(replay, segment, status);
    }

    bool finish_endpoint_replay_redrive(endpoint_replay_record& replay)
    {
        if (replay.replay_len != replay.len) {
            fail_endpoint_replay_redrive(replay, tlm::TLM_BURST_ERROR_RESPONSE);
            return false;
        }

        replay.redriven = true;
        replay.replay_status = tlm::TLM_OK_RESPONSE;
        return true;
    }

    void fail_endpoint_replay_redrive(endpoint_replay_record& replay,
                                      tlm::tlm_response_status status)
    {
        replay.replay_status = status;
        replay.succeeded = false;
    }

    const endpoint_replay_record* find_endpoint_replay(uint32_t stream_id,
                                                       uint16_t stag) const
    {
        if (stag == 0) {
            return nullptr;
        }
        for (const auto& replay : m_endpoint_replay_records) {
            if (replay.stream_id == stream_id && replay.stag == stag) {
                return &replay;
            }
        }
        return nullptr;
    }

    void reset_endpoint_replay_records()
    {
        m_endpoint_replay_records = {};
    }

    queue_state& cmdq_state()
    {
        return m_cmdq_state;
    }

    const queue_state& cmdq_state() const
    {
        return m_cmdq_state;
    }

    queue_state& eventq_state()
    {
        return m_eventq_state;
    }

    const queue_state& eventq_state() const
    {
        return m_eventq_state;
    }

    queue_state& priq_state()
    {
        return m_priq_state;
    }

    const queue_state& priq_state() const
    {
        return m_priq_state;
    }

    void reset_register_queue_state()
    {
        m_cmdq_state = {};
        m_eventq_state = {};
        m_priq_state = {};
    }

    stream_context_descriptor_state& stream_context_state()
    {
        return m_stream_context_state;
    }

    const stream_context_descriptor_state& stream_context_state() const
    {
        return m_stream_context_state;
    }

    void reset_stream_context_descriptor_state()
    {
        m_stream_context_state = {};
    }

    page_table_walker_state& walker_state()
    {
        return m_walker_state;
    }

    const page_table_walker_state& walker_state() const
    {
        return m_walker_state;
    }

    void reset_page_table_walker_state()
    {
        m_walker_state = {};
    }

    fault_replay_state& fault_replay_state_storage()
    {
        return m_fault_replay_state;
    }

    const fault_replay_state& fault_replay_state_storage() const
    {
        return m_fault_replay_state;
    }

    void reset_fault_replay_state()
    {
        m_fault_replay_state = {};
    }

    static constexpr const char* canonical_owner_name()
    {
        return "apollo_smmu_tbu/SystemC";
    }

private:
    ownership_contract m_ownership {};
    state_ownership_contract m_state_ownership {};
    queue_state m_cmdq_state {};
    queue_state m_eventq_state {};
    queue_state m_priq_state {};
    stream_context_descriptor_state m_stream_context_state {};
    page_table_walker_state m_walker_state {};
    fault_replay_state m_fault_replay_state {};
    stall_record_table m_stall_records {};
    endpoint_replay_table m_endpoint_replay_records {};
};

} // namespace apollo::smmuv3
