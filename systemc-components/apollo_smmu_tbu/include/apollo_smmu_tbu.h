/*
 * Apollo functional SMMU/TBU bridge.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <vector>

#include <apollo_smmu_arch_core.h>
#include <cci_configuration>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <tlm-extensions/apollo-smmu-stream-id.h>
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
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq_out;

    cci::cci_param<uint32_t> p_stream_id;
    cci::cci_param<uint64_t> p_iova_base;
    cci::cci_param<uint64_t> p_pa_base;
    cci::cci_param<uint64_t> p_window_size;

    explicit apollo_smmu_tbu(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , upstream("upstream")
        , regs("regs")
        , downstream("downstream")
        , irq_out("irq_out", 4, [](const char* n, size_t) { return new InitiatorSignalSocket<bool>(n); })
        , p_stream_id("stream_id", 1, "StreamID associated with this TBU")
        , p_iova_base("iova_base", 0x10000000ULL, "Base IOVA accepted by this TBU")
        , p_pa_base("pa_base", 0x00a00000ULL, "Translated physical base for the IOVA window")
        , p_window_size("window_size", 0x00600000ULL, "Size of translated IOVA window")
        , m_arch_stalls(m_arch_core.stall_records())
        , m_arch_endpoint_replays(m_arch_core.endpoint_replay_records())
        , m_cmdq(m_arch_core.cmdq_state())
        , m_eventq(m_arch_core.eventq_state())
        , m_priq(m_arch_core.priq_state())
        , m_arch_stream_context(m_arch_core.stream_context_state())
        , m_arch_ste_base(m_arch_stream_context.compatibility_ste_base)
        , m_arch_cd_base(m_arch_stream_context.current_cd_base)
        , m_arch_stream_id(m_arch_stream_context.selected_stream_id)
        , m_arch_stream_id_valid(m_arch_stream_context.selected_stream_id_valid)
        , m_arch_selected_ssid(m_arch_stream_context.selected_ssid)
        , m_arch_selected_ssid_valid(m_arch_stream_context.selected_ssid_valid)
        , m_arch_strtab_cfg(m_arch_stream_context.strtab_cfg)
        , m_arch_strtab_base(m_arch_stream_context.strtab_base)
        , m_arch_walker(m_arch_core.walker_state())
        , m_arch_ttbr(m_arch_walker.ttbr)
        , m_arch_iova(m_arch_walker.iova)
        , m_arch_last_ipa(m_arch_walker.last_ipa)
        , m_arch_last_fetch_addr(m_arch_walker.last_fetch_addr)
        , m_arch_s2ttb(m_arch_walker.s2ttb)
        , m_arch_last_desc(m_arch_walker.last_desc)
        , m_arch_last_pa(m_arch_walker.last_pa)
        , m_arch_walk_depth(m_arch_walker.walk_depth)
        , m_arch_last_stage(m_arch_walker.last_stage)
        , m_arch_fault_replay(m_arch_core.fault_replay_state_storage())
        , m_arch_fault_reason(m_arch_fault_replay.fault_reason)
        , m_arch_fault_stage(m_arch_fault_replay.fault_stage)
        , m_arch_fault_event_class(m_arch_fault_replay.fault_event_class)
        , m_arch_fault_nsipa(m_arch_fault_replay.fault_nsipa)
        , m_arch_fault_gpcf(m_arch_fault_replay.fault_gpcf)
        , m_arch_fault_record_suppressed(m_arch_fault_replay.fault_record_suppressed)
        , m_arch_last_fault_detail(m_arch_fault_replay.last_fault_detail)
        , m_arch_fault_replays(m_arch_fault_replay.fault_replays)
        , m_arch_ats_responses(m_arch_fault_replay.ats_responses)
        , m_arch_pri_responses(m_arch_fault_replay.pri_responses)
        , m_arch_ats_success(m_arch_fault_replay.ats_success)
        , m_arch_ats_ur(m_arch_fault_replay.ats_ur)
        , m_arch_ats_ca(m_arch_fault_replay.ats_ca)
        , m_arch_pri_accepted(m_arch_fault_replay.pri_accepted)
        , m_arch_pri_rejected(m_arch_fault_replay.pri_rejected)
        , m_arch_pri_unknown(m_arch_fault_replay.pri_unknown)
        , m_arch_pri_auto_responses(m_arch_fault_replay.pri_auto_responses)
        , m_arch_pri_auto_failures(m_arch_fault_replay.pri_auto_failures)
        , m_arch_stall_pending(m_arch_fault_replay.stall_pending)
        , m_arch_stall_retried(m_arch_fault_replay.stall_retried)
        , m_arch_stall_terminated(m_arch_fault_replay.stall_terminated)
        , m_arch_stall_buffered(m_arch_fault_replay.stall_buffered)
        , m_arch_stall_redriven(m_arch_fault_replay.stall_redriven)
        , m_arch_stall_suppressed(m_arch_fault_replay.stall_suppressed)
        , m_arch_stall_merged(m_arch_fault_replay.stall_merged)
        , m_arch_resume_unknown(m_arch_fault_replay.resume_unknown)
        , m_arch_endpoint_replay_pending(m_arch_fault_replay.endpoint_replay_pending)
        , m_arch_endpoint_replay_retried(m_arch_fault_replay.endpoint_replay_retried)
        , m_arch_endpoint_replay_succeeded(m_arch_fault_replay.endpoint_replay_succeeded)
        , m_arch_endpoint_replay_terminated(m_arch_fault_replay.endpoint_replay_terminated)
        , m_arch_endpoint_replay_redriven(m_arch_fault_replay.endpoint_replay_redriven)
        , m_arch_endpoint_replay_failed(m_arch_fault_replay.endpoint_replay_failed)
        , m_arch_endpoint_block_waits(m_arch_fault_replay.endpoint_block_waits)
        , m_arch_endpoint_block_resumed(m_arch_fault_replay.endpoint_block_resumed)
        , m_arch_endpoint_block_failed(m_arch_fault_replay.endpoint_block_failed)
        , m_arch_endpoint_blocking_enabled(m_arch_fault_replay.endpoint_blocking_enabled)
        , m_arch_early_retry_attempted(m_arch_fault_replay.early_retry_attempted)
        , m_arch_early_retry_succeeded(m_arch_fault_replay.early_retry_succeeded)
        , m_arch_early_retry_failed(m_arch_fault_replay.early_retry_failed)
        , m_arch_early_retry_discarded(m_arch_fault_replay.early_retry_discarded)
        , m_arch_next_fault_replay_id(m_arch_fault_replay.next_fault_replay_id)
        , m_arch_next_stag(m_arch_fault_replay.next_stag)
        , m_arch_last_stag(m_arch_fault_replay.last_stag)
        , m_arch_last_resume_stag(m_arch_fault_replay.last_resume_stag)
        , m_arch_next_prg(m_arch_fault_replay.next_prg)
        , m_arch_last_prg(m_arch_fault_replay.last_prg)
        , m_arch_last_auto_prg(m_arch_fault_replay.last_auto_prg)
        , m_arch_last_auto_response(m_arch_fault_replay.last_auto_response)
    {
        upstream.register_b_transport(this, &apollo_smmu_tbu::b_transport);
        upstream.register_transport_dbg(this, &apollo_smmu_tbu::transport_dbg);
        regs.register_b_transport(this, &apollo_smmu_tbu::regs_b_transport);
        regs.register_transport_dbg(this, &apollo_smmu_tbu::regs_transport_dbg);
    }

#ifdef APOLLO_SMMU_TBU_TESTING
public:
#else
private:
#endif
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
        REG_MAP_STREAM_ID = 0x90,
        REG_MAP_STREAM_COUNT = 0x94,
        REG_ARCH_STREAM_ID = 0x98,
        REG_ARCH_ATS_DETAIL = 0x9c,
        REG_ARCH_PRI_DETAIL = 0xa0,
        REG_ARCH_FAULT_DETAIL = 0xa4,
        REG_ARCH_STALL_STATUS = 0xa8,
        REG_ARCH_IPA_LO = 0xac,
        REG_ARCH_IPA_HI = 0xb0,
        REG_ARCH_CMD_STATUS = 0xb4,
        REG_ARCH_CMD_DETAIL = 0xb8,
        REG_ARCH_SSID = 0xbc,
        REG_ARCH_CD_DETAIL = 0xc0,
        REG_ARCH_ENDPOINT_REPLAY_STATUS = 0xc4,
        REG_ARCH_ENDPOINT_REPLAY_CTRL = 0xc8,
        REG_ARCH_ENDPOINT_BLOCK_STATUS = 0xcc,
        REG_ARCH_STALL_MERGE_STATUS = 0xd0,
        REG_ARCH_EARLY_RETRY_STATUS = 0xd4,
        REG_ARCH_MPAM_STATUS = 0xd8,
        REG_ARCH_MPAM_DETAIL = 0xdc,
        REG_ARCH_SECURITY_STATUS = 0xe0,
        REG_ARCH_PAR_LO = 0xe4,
	        REG_ARCH_PAR_HI = 0xe8,
	        REG_SMMUV3_BASE = 0x1000,
	        SMMUV3_SECURE_PAGE = 0x8000,
	        SMMUV3_VATOS_PAGE = 0x20000,
	        SMMUV3_S_VATOS_PAGE = 0x30000,
	        SMMUV3_IDR0 = 0x000,
        SMMUV3_IDR1 = 0x004,
        SMMUV3_IDR2 = 0x008,
        SMMUV3_IDR3 = 0x00c,
        SMMUV3_IDR4 = 0x010,
        SMMUV3_IDR5 = 0x014,
        SMMUV3_IIDR = 0x018,
        SMMUV3_AIDR = 0x01c,
        SMMUV3_CR0 = 0x020,
        SMMUV3_CR0ACK = 0x024,
        SMMUV3_CR1 = 0x028,
        SMMUV3_CR2 = 0x02c,
        SMMUV3_STATUSR = 0x040,
        SMMUV3_GBPA = 0x044,
        SMMUV3_AGBPA = 0x048,
        SMMUV3_IRQ_CTRL = 0x050,
        SMMUV3_IRQ_CTRLACK = 0x054,
        SMMUV3_GERROR = 0x060,
        SMMUV3_GERRORN = 0x064,
        SMMUV3_GERROR_IRQ_CFG0 = 0x068,
        SMMUV3_GERROR_IRQ_CFG0_HI = 0x06c,
        SMMUV3_GERROR_IRQ_CFG1 = 0x070,
        SMMUV3_GERROR_IRQ_CFG2 = 0x074,
        SMMUV3_STRTAB_BASE_LO = 0x080,
        SMMUV3_STRTAB_BASE_HI = 0x084,
        SMMUV3_STRTAB_BASE_CFG = 0x088,
        SMMUV3_CMDQ_BASE_LO = 0x090,
        SMMUV3_CMDQ_BASE_HI = 0x094,
        SMMUV3_CMDQ_PROD = 0x098,
        SMMUV3_CMDQ_CONS = 0x09c,
        SMMUV3_EVENTQ_BASE_LO = 0x0a0,
        SMMUV3_EVENTQ_BASE_HI = 0x0a4,
        SMMUV3_EVENTQ_PROD = 0x0a8,
        SMMUV3_EVENTQ_CONS = 0x0ac,
        SMMUV3_EVENTQ_IRQ_CFG0 = 0x0b0,
        SMMUV3_EVENTQ_IRQ_CFG0_HI = 0x0b4,
        SMMUV3_EVENTQ_IRQ_CFG1 = 0x0b8,
        SMMUV3_EVENTQ_IRQ_CFG2 = 0x0bc,
        SMMUV3_PRIQ_BASE_LO = 0x0c0,
        SMMUV3_PRIQ_BASE_HI = 0x0c4,
        SMMUV3_PRIQ_PROD = 0x0c8,
        SMMUV3_PRIQ_CONS = 0x0cc,
        SMMUV3_PRIQ_IRQ_CFG0 = 0x0d0,
        SMMUV3_PRIQ_IRQ_CFG0_HI = 0x0d4,
        SMMUV3_PRIQ_IRQ_CFG1 = 0x0d8,
        SMMUV3_PRIQ_IRQ_CFG2 = 0x0dc,
        SMMUV3_STATUS = 0x0e0,
        SMMUV3_GATOS_CTRL = 0x100,
        SMMUV3_GATOS_SID_LO = 0x108,
        SMMUV3_GATOS_SID_HI = 0x10c,
        SMMUV3_GATOS_ADDR_LO = 0x110,
        SMMUV3_GATOS_ADDR_HI = 0x114,
        SMMUV3_GATOS_PAR_LO = 0x118,
	        SMMUV3_GATOS_PAR_HI = 0x11c,
        SMMUV3_MPAMIDR = 0x130,
        SMMUV3_GMPAM = 0x138,
        SMMUV3_GBPMPAM = 0x13c,
        SMMUV3_VATOS_SEL = 0x180,
        SMMUV3_IDR6 = 0x190,
        SMMUV3_DPT_BASE_LO = 0x200,
        SMMUV3_DPT_BASE_HI = 0x204,
        SMMUV3_DPT_BASE_CFG = 0x208,
        SMMUV3_DPT_CFG_FAR_LO = 0x210,
        SMMUV3_DPT_CFG_FAR_HI = 0x214,
        SMMUV3_CMDQ_CONTROL_PAGE_BASE_LO = 0x4000,
        SMMUV3_CMDQ_CONTROL_PAGE_BASE_HI = 0x4004,
        SMMUV3_CMDQ_CONTROL_PAGE_CFG = 0x4008,
        SMMUV3_CMDQ_CONTROL_PAGE_STATUS = 0x400c,
        SMMUV3_S_CMDQ_CONTROL_PAGE_BASE_LO =
            SMMUV3_SECURE_PAGE + SMMUV3_CMDQ_CONTROL_PAGE_BASE_LO,
        SMMUV3_S_CMDQ_CONTROL_PAGE_BASE_HI =
            SMMUV3_SECURE_PAGE + SMMUV3_CMDQ_CONTROL_PAGE_BASE_HI,
        SMMUV3_S_CMDQ_CONTROL_PAGE_CFG =
            SMMUV3_SECURE_PAGE + SMMUV3_CMDQ_CONTROL_PAGE_CFG,
        SMMUV3_S_CMDQ_CONTROL_PAGE_STATUS =
            SMMUV3_SECURE_PAGE + SMMUV3_CMDQ_CONTROL_PAGE_STATUS,
        SMMUV3_VATOS_CTRL = 0x0a00,
	        SMMUV3_VATOS_SID_LO = 0x0a08,
	        SMMUV3_VATOS_SID_HI = 0x0a0c,
	        SMMUV3_VATOS_ADDR_LO = 0x0a10,
	        SMMUV3_VATOS_ADDR_HI = 0x0a14,
	        SMMUV3_VATOS_PAR_LO = 0x0a18,
	        SMMUV3_VATOS_PAR_HI = 0x0a1c,
	    };

    enum : uint32_t {
        MAP_CTRL_ADD = 1,
        MAP_CTRL_REMOVE = 2,
        MAP_CTRL_CLEAR = 3,
        FAULT_CTRL_CLEAR = 1,
        FAULT_CTRL_INJECT = 2,
        ARCH_CTRL_PROBE = 1,
        ARCH_CTRL_NEGATIVE_REPLAY = 2,
        ARCH_CTRL_PROBE_WRITE = 3,
        ARCH_CTRL_ATS_TRANSLATION_REQUEST = 4,
        ARCH_CTRL_NEGATIVE_REPLAY_WRITE = 5,
        ARCH_CTRL_RECORD_F_UUT = 6,
        ARCH_CTRL_TLB_CONFLICT = 7,
        ARCH_CTRL_CFG_CONFLICT = 8,
        ARCH_CTRL_GATOS_TRANSLATE = 9,
        ARCH_CTRL_ATS_TRANSLATION_REQUEST_WRITE = 10,
        ARCH_GATOS_CTRL_RUN = 1u << 0,
        ARCH_ATOS_ADDR_IND = 1u << 7,
        ARCH_ATOS_ADDR_RNW = 1u << 8,
        ARCH_ATOS_ADDR_PNU = 1u << 9,
        ARCH_ATOS_ADDR_TYPE_SHIFT = 10,
        ARCH_ATOS_ADDR_TYPE_MASK = 0x3,
        ARCH_ATOS_ADDR_TYPE_RESERVED = 0x0,
        ARCH_ATOS_ADDR_TYPE_STAGE1 = 0x1,
        ARCH_ATOS_ADDR_TYPE_STAGE2 = 0x2,
        ARCH_ATOS_ADDR_TYPE_STAGE1_STAGE2 = 0x3,
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
        FEATURE_ARCH_REG_QUEUE_SURFACE = 1u << 10,
        FEATURE_MULTI_STREAM_ID = 1u << 11,
        FEATURE_ARCH_CMD_INVALIDATION = 1u << 12,
        FEATURE_ARCH_TAGGED_INVALIDATION = 1u << 13,
        FEATURE_ARCH_CD_TABLE_INDEX = 1u << 14,
        FEATURE_ARCH_RESERVED_ENCODING_CHECKS = 1u << 15,
        FEATURE_ARCH_EVENT_RECORD_LAYOUT = 1u << 16,
        FEATURE_ENDPOINT_SUBSTREAM_ID = 1u << 17,
        FEATURE_ARCH_CR0_QUEUE_GATES = 1u << 18,
        FEATURE_ARCH_ATSCHK_EATS_GATES = 1u << 19,
        FEATURE_ARCH_REC_CFG_ATS_GATES = 1u << 20,
        FEATURE_ARCH_DPTI_UNSUPPORTED = 1u << 21,
        FEATURE_ARCH_CMDQ_CERROR = 1u << 22,
        FEATURE_ARCH_QUEUE_OVERFLOW_FLAGS = 1u << 23,
        FEATURE_ARCH_STALL_BUFFER_REDRIVE = 1u << 24,
        FEATURE_ARCH_PRI_AUTO_RESPONSE = 1u << 25,
        FEATURE_ARCH_IRQ_MSI_CFG = 1u << 26,
        FEATURE_ARCH_CONFIG_DISABLED_NO_EVENT = 1u << 27,
        FEATURE_ARCH_F_UUT_EVENT = 1u << 28,
        FEATURE_ARCH_VMS_FETCH = 1u << 29,
        FEATURE_ARCH_CFGI_VMS_PIDM = 1u << 30,
        PAGE_SHIFT = 12,
        PAGE_SIZE = 1u << PAGE_SHIFT,
    };

    enum : uint64_t {
        ARCH_INDEX_MASK = 0x1ff,
        ARCH_LEVELS = 4,
        ARCH_LEVEL_BITS = 9,
        ARCH_L0_SHIFT = 39,
        ARCH_DESC_TYPE_MASK = 0x3,
        ARCH_DESC_BLOCK = 0x1,
        ARCH_DESC_TABLE = 0x3,
        ARCH_DESC_PAGE = 0x3,
        ARCH_DESC_OUTPUT_MASK = 0x0000fffffffff000ULL,
        ARCH_GATOS_PAR_ADDR_MASK = 0x00fffffffffff000ULL,
        ARCH_ATOS_ADDR_ADDR_MASK = 0xfffffffffffff000ULL,
        ARCH_ATOS_SID_SUBSTREAMID_MASK = 0xfffffULL,
        ARCH_ATOS_SID_SSID_VALID = 1ULL << 52,
        ARCH_ATOS_SID_SECURE_STREAM = 1ULL << 53,
        ARCH_DESC_NS = 1ULL << 5,
        ARCH_DESC_AF = 1ULL << 10,
        ARCH_DESC_AP_RO = 1ULL << 7,
        ARCH_DESC_NT = 1ULL << 16,
        ARCH_DESC_DBM = 1ULL << 51,
        ARCH_DESC_PXN = 1ULL << 53,
        ARCH_DESC_UXN = 1ULL << 54,
        ARCH_DESC_PXNTABLE = 1ULL << 59,
        ARCH_DESC_UXNTABLE = 1ULL << 60,
        ARCH_DESC_APTABLE_NO_UNPRIV = 1ULL << 61,
        ARCH_DESC_APTABLE_RO = 1ULL << 62,
        ARCH_DESC_NSTABLE = 1ULL << 63,
        ARCH_DESC_S2_MEMATTR_SHIFT = 2,
        ARCH_DESC_S2_MEMATTR_MASK = 0xf,
        ARCH_DESC_S2_MEMATTR_NORMAL_NC = 0x4,
        ARCH_STE_SIZE = 64,
        ARCH_CD_SIZE = 64,
        ARCH_STE_VALID = 1u << 0,
        ARCH_STE_S1_ENABLED = 1u << 1,
        ARCH_STE_CFG_SHIFT = 1,
        ARCH_STE_CFG_MASK = 0x7,
        ARCH_STE_CFG_BYPASS = 0x4,
        ARCH_STE_CFG_ABORT = ARCH_STE_CFG_BYPASS,
        ARCH_STE_CFG_S1_TRANS = 0x5,
        ARCH_STE_CFG_S2_TRANS = 0x6,
        ARCH_STE_CFG_NESTED = 0x7,
        ARCH_STE_S1CTXPTR_MASK = 0x0000ffffffffffc0ULL,
        ARCH_STE_S2TTB_MASK = 0x0000fffffffffff0ULL,
        ARCH_STE_S1FMT_SHIFT = 4,
        ARCH_STE_S1FMT_MASK = 0x3,
        ARCH_STE_S1FMT_LINEAR = 0,
        ARCH_STE_S1FMT_64K_L2 = 2,
        ARCH_STE_S1CDMAX_SHIFT = 59,
        ARCH_STE_S1CDMAX_MASK = 0x1f,
        ARCH_STE_S1DSS_MASK = 0x3,
        ARCH_STE_EATS_SHIFT = 28,
        ARCH_STE_EATS_MASK = 0x3,
        ARCH_STE_EATS_DISABLED = 0,
        ARCH_STE_EATS_FULL = 1,
        ARCH_STE_EATS_SPLIT = 2,
        ARCH_STE_EATS_DPT = 3,
        ARCH_STE_S1DSS_TERMINATE = 0,
        ARCH_STE_S1DSS_BYPASS = 1,
        ARCH_STE_S1DSS_SSID0 = 2,
        ARCH_STE_NSCFG_SHIFT = 46,
        ARCH_STE_NSCFG_MASK = 0x3,
        ARCH_STE_NSCFG_USE_INCOMING = 0,
        ARCH_STE_NSCFG_RESERVED = 1,
        ARCH_STE_NSCFG_SECURE = 2,
        ARCH_STE_NSCFG_NONSECURE = 3,
        ARCH_STE_MTCFG = 1ULL << 4,
        ARCH_STE_MEMATTR_SHIFT = 5,
        ARCH_STE_MEMATTR_MASK = 0xf,
        ARCH_STE_SHCFG_SHIFT = 9,
        ARCH_STE_SHCFG_MASK = 0x3,
        ARCH_STE_SHCFG_OSH = 0x2,
        ARCH_STE_ALLOCCFG_SHIFT = 11,
        ARCH_STE_ALLOCCFG_MASK = 0xf,
        ARCH_STE_PRIVCFG_SHIFT = 15,
        ARCH_STE_PRIVCFG_MASK = 0x3,
        ARCH_STE_INSTCFG_SHIFT = 17,
        ARCH_STE_INSTCFG_MASK = 0x3,
        ARCH_STE_PPAR = 1ULL << 18,
        ARCH_STE_S1MPAM = 1ULL << 26,
        ARCH_STE_S2S = 1ULL << 48,
        ARCH_STE_S2R = 1ULL << 49,
        ARCH_STE_S2PTW = 1ULL << 54,
        ARCH_STE_S2HD = 1ULL << 55,
        ARCH_STE_S2HA = 1ULL << 56,
        ARCH_STE_S2HAFT = 1ULL << 59,
        ARCH_GBPA_MEMATTR_SHIFT = 0,
        ARCH_GBPA_MEMATTR_MASK = 0xf,
        ARCH_GBPA_MTCFG = 1u << 4,
        ARCH_GBPA_ALLOCCFG_SHIFT = 8,
        ARCH_GBPA_ALLOCCFG_MASK = 0xf,
        ARCH_GBPA_SHCFG_SHIFT = 12,
        ARCH_GBPA_SHCFG_MASK = 0x3,
        ARCH_GBPA_PRIVCFG_SHIFT = 16,
        ARCH_GBPA_PRIVCFG_MASK = 0x3,
        ARCH_GBPA_INSTCFG_SHIFT = 18,
        ARCH_GBPA_INSTCFG_MASK = 0x3,
        ARCH_GBPA_ABORT = 1u << 20,
        ARCH_GBPA_UPDATE = 1u << 31,
        ARCH_GBPA_KNOWN_MASK = ARCH_GBPA_MEMATTR_MASK | ARCH_GBPA_MTCFG |
                                (ARCH_GBPA_ALLOCCFG_MASK << ARCH_GBPA_ALLOCCFG_SHIFT) |
                                (ARCH_GBPA_SHCFG_MASK << ARCH_GBPA_SHCFG_SHIFT) |
                                (ARCH_GBPA_PRIVCFG_MASK << ARCH_GBPA_PRIVCFG_SHIFT) |
                                (ARCH_GBPA_INSTCFG_MASK << ARCH_GBPA_INSTCFG_SHIFT) |
                                ARCH_GBPA_ABORT,
        ARCH_AGBPA_UNSUPPORTED_RES0 = 0,
        ARCH_DPT_UNSUPPORTED_RES0 = 0,
        ARCH_ECMDQ_UNSUPPORTED_RES0 = 0,
        ARCH_STE_S2VMID_SHIFT = 48,
        ARCH_STE_S2VMID_MASK = 0xffff,
        ARCH_STALL_MODEL_TERMINATE_ONLY = 0x1,
        ARCH_STALL_MODEL_STALL = 0x2,
        ARCH_STE_MPAM_WORD4_OFFSET = 4 * sizeof(uint64_t),
        ARCH_STE_PARTID_SHIFT = 16,
        ARCH_STE_PARTID_MASK = 0xffff,
        ARCH_STE_PMG_SHIFT = 0,
        ARCH_STE_PMG_MASK = 0xff,
        ARCH_STE_VMSPTR_OFFSET = 5 * sizeof(uint64_t),
        ARCH_STE_S_S2TTB_WORD_OFFSET = 6 * sizeof(uint64_t),
        ARCH_STE_VMSPTR_MASK = ARCH_DESC_OUTPUT_MASK,
        ARCH_STE_S_S2TTB_MASK = ARCH_STE_S2TTB_MASK,
        ARCH_VMS_PARTID_MAP_WORDS = 8,
        ARCH_VMS_PARTID_MAP_ENTRIES = 32,
        ARCH_CD_MPAM_WORD_OFFSET = 5 * sizeof(uint64_t),
        ARCH_CD_PARTID_SHIFT = 32,
        ARCH_CD_PARTID_MASK = 0xffff,
        ARCH_CD_VIRTUAL_PARTID_MASK = 0x1f,
        ARCH_CD_PMG_SHIFT = 48,
        ARCH_CD_PMG_MASK = 0xff,
        ARCH_MPAM_UNKNOWN_PARTID = 0xffff,
        ARCH_MPAM_UNKNOWN_PMG = 0xff,
        ARCH_MPAM_REG_PARTID_MASK = 0xffff,
        ARCH_MPAM_REG_PMG_SHIFT = 16,
        ARCH_MPAM_REG_PMG_MASK = 0xff,
        ARCH_MPAM_REG_VALUE_MASK = 0x00ffffff,
        ARCH_MPAM_SPACE_SECURE = 0,
        ARCH_MPAM_SPACE_NONSECURE = 1,
        ARCH_MPAM_SPACE_ROOT = 2,
        ARCH_MPAM_SPACE_REALM = 3,
        ARCH_SECURITY_NONSECURE = 0,
        ARCH_SECURITY_SECURE = 1,
        ARCH_SECURITY_REALM = 2,
        ARCH_SECURITY_ROOT = 3,
        ARCH_SECURITY_ANY = 0xff,
        ARCH_SECURITY_EVENTQ_STATE_SHIFT = 16,
        ARCH_SECURITY_EVENTQ_STATE_MASK = 0x3,
        ARCH_CD_L1_DESC_VALID = 1ULL << 0,
        ARCH_CD_L1_DESC_L2PTR_MASK = 0x0000fffffffff000ULL,
        ARCH_CD_L2_ENTRIES = 1024,
        ARCH_CD_VALID = 1u << 0,
        ARCH_CD_VALID_ARCHITECTED = 1ULL << 31,
        ARCH_CD_HD = 1ULL << 42,
        ARCH_CD_HA = 1ULL << 43,
        ARCH_CD_ASID_SHIFT = 48,
        ARCH_CD_ASID_MASK = 0xffffULL << ARCH_CD_ASID_SHIFT,
        ARCH_CD_NSCFG0 = 1ULL << 0,
        ARCH_CD_HAD0 = 1ULL << 1,
        ARCH_CD_HAD1 = 1ULL << 1,
        ARCH_CD_E0PD0 = 1ULL << 2,
        ARCH_CD_E0PD1 = 1ULL << 2,
        ARCH_CD_HAFT = 1ULL << 3,
        ARCH_CD_TTB0_MASK = 0x0000fffffffffff0ULL,
        ARCH_FAULT_NONE = 0,
        ARCH_FAULT_STE_FETCH = 1,
        ARCH_FAULT_STE_INVALID = 2,
        ARCH_FAULT_CD_FETCH = 3,
        ARCH_FAULT_CD_INVALID = 4,
        ARCH_FAULT_TABLE_INVALID = 5,
        ARCH_FAULT_PAGE_INVALID = 6,
        ARCH_FAULT_NEGATIVE_UNEXPECTED_PASS = 7,
        ARCH_FAULT_BAD_STREAM_ID = 8,
        ARCH_FAULT_ACCESS = 9,
        ARCH_FAULT_PERMISSION = 10,
        ARCH_FAULT_ADDR_SIZE = 11,
        ARCH_FAULT_GRANULE = 12,
        ARCH_FAULT_STAGE2 = 13,
        ARCH_FAULT_BAD_ATS_TREQ = 14,
        ARCH_FAULT_WALK_EABT = 15,
        ARCH_FAULT_VMS_FETCH = 16,
        ARCH_FAULT_STREAM_DISABLED = 17,
        ARCH_FAULT_BAD_SUBSTREAMID = 18,
        ARCH_FAULT_TRANSL_FORBIDDEN = 19,
        ARCH_FAULT_TLB_CONFLICT = 20,
        ARCH_FAULT_CFG_CONFLICT = 21,
        ARCH_FAULT_UNSUPPORTED_UPSTREAM = 22,
        ARCH_FAULT_ATOS_INV_STAGE = 23,
        ARCH_FAULT_ATOS_INV_REQ = 24,
        ARCH_STRTAB_BASE_ADDR_MASK = 0x0000ffffffffffc0ULL,
        ARCH_STRTAB_L1_DESC_SPAN_MASK = 0x1f,
        ARCH_STRTAB_L1_DESC_L2PTR_MASK = 0x0000ffffffffffc0ULL,
        ARCH_QUEUE_BASE_MASK = 0x0000ffffffffffe0ULL,
        ARCH_CMDQ_ENTRY_BYTES = 16,
        ARCH_EVENTQ_ENTRY_BYTES = 32,
        ARCH_PRIQ_ENTRY_BYTES = 32,
        ARCH_PRIQ_PPR_TYPE = 0x2,
        ARCH_PRIQ_PPR_SSV = 1ull << 8,
        ARCH_PRIQ_PPR_LAST = 1ull << 9,
        ARCH_PRIQ_PPR_WRITE = 1ull << 10,
        ARCH_PRIQ_PPR_READ = 1ull << 11,
        ARCH_PRIQ_PPR_EXEC = 1ull << 12,
        ARCH_PRIQ_PPR_PRIV = 1ull << 13,
        ARCH_PRIQ_PPR_SSID_SHIFT = 28,
        ARCH_PRIQ_PPR_SSID_MASK = 0xfffff,
        ARCH_PRIQ_PPR_PRG_MASK = 0x1ff,
        ARCH_PRIQ_PPR_LEN_MASK = (1ull << ARCH_PRIQ_PPR_SSID_SHIFT) - 1,
        ARCH_QUEUE_OVFLG = 1u << 31,
        ARCH_QUEUE_INDEX_MASK = ARCH_QUEUE_OVFLG - 1,
        ARCH_EVENT_IPA_MASK = 0x000ffffffffff000ULL,
        ARCH_EVENT_FETCH_ADDR_MASK = 0x000ffffffffff8ULL,
    };

	    enum : uint32_t {
	        ARCH_IDR0_S2P = 1u << 0,
	        ARCH_IDR0_S1P = 1u << 1,
	        ARCH_IDR0_TTF_SHIFT = 2,
	        ARCH_IDR0_TTF_MASK = 0x3u << ARCH_IDR0_TTF_SHIFT,
	        ARCH_IDR0_TTF_AARCH64 = 0x2u << ARCH_IDR0_TTF_SHIFT,
	        ARCH_IDR0_HTTU_SHIFT = 6,
	        ARCH_IDR0_HTTU_MASK = 0x3u << ARCH_IDR0_HTTU_SHIFT,
	        ARCH_IDR0_HTTU_ACCESS_DIRTY = 0x2u << ARCH_IDR0_HTTU_SHIFT,
	        ARCH_IDR0_HTTU_ACCESS_DIRTY_TABLE =
                    0x3u << ARCH_IDR0_HTTU_SHIFT,
	        ARCH_IDR0_DORMHINT = 1u << 8,
	        ARCH_IDR0_HYP = 1u << 9,
	        ARCH_IDR0_ATS = 1u << 10,
	        ARCH_IDR0_ASID16 = 1u << 12,
	        ARCH_IDR0_ATOS = 1u << 15,
	        ARCH_IDR0_PRI = 1u << 16,
	        ARCH_IDR0_VMID16 = 1u << 18,
	        ARCH_IDR0_CD2L = 1u << 19,
	        ARCH_IDR0_VATOS = 1u << 20,
	        ARCH_IDR0_ATSRECERR = 1u << 23,
	        ARCH_IDR0_STALL_MODEL_TERMINATE_ONLY = 1u << 24,
	        ARCH_IDR0_MSI = 1u << 13,
	        ARCH_IDR0_ST_LEVEL_SHIFT = 27,
	        ARCH_IDR0_ST_LEVEL_MASK = 0x3u << ARCH_IDR0_ST_LEVEL_SHIFT,
	        ARCH_IDR0_ST_LEVEL_2LVL = 0x1u << ARCH_IDR0_ST_LEVEL_SHIFT,
	        ARCH_IDR0 = ARCH_IDR0_S2P | ARCH_IDR0_S1P |
                            ARCH_IDR0_TTF_AARCH64 | ARCH_IDR0_DORMHINT |
                            ARCH_IDR0_HTTU_ACCESS_DIRTY_TABLE |
                            ARCH_IDR0_HYP | ARCH_IDR0_ATS |
                            ARCH_IDR0_ASID16 | ARCH_IDR0_MSI |
                            ARCH_IDR0_ATOS | ARCH_IDR0_PRI |
                            ARCH_IDR0_VMID16 | ARCH_IDR0_CD2L |
                            ARCH_IDR0_ATSRECERR |
                            ARCH_IDR0_STALL_MODEL_TERMINATE_ONLY |
                            ARCH_IDR0_ST_LEVEL_2LVL,
        ARCH_IDR1_SIDSIZE_SHIFT = 0,
        ARCH_IDR1_SIDSIZE_MASK = 0x3fu << ARCH_IDR1_SIDSIZE_SHIFT,
        ARCH_IDR1_SIDSIZE = 8,
        ARCH_IDR1_SSIDSIZE_SHIFT = 6,
        ARCH_IDR1_SSIDSIZE_MASK = 0x1fu << ARCH_IDR1_SSIDSIZE_SHIFT,
        ARCH_IDR1_SSIDSIZE = 20,
        ARCH_IDR1_PRIQS_SHIFT = 11,
        ARCH_IDR1_PRIQS_MASK = 0x1fu << ARCH_IDR1_PRIQS_SHIFT,
        ARCH_IDR1_EVENTQS_SHIFT = 16,
        ARCH_IDR1_EVENTQS_MASK = 0x1fu << ARCH_IDR1_EVENTQS_SHIFT,
        ARCH_IDR1_CMDQS_SHIFT = 21,
        ARCH_IDR1_CMDQS_MASK = 0x1fu << ARCH_IDR1_CMDQS_SHIFT,
        ARCH_IDR1_QUEUE_LOG2_MAX = 15,
        ARCH_IDR1_ATTR_PERMS_OVR = 1u << 26,
        ARCH_IDR1_ATTR_TYPES_OVR = 1u << 27,
        ARCH_IDR1_ECMDQ = 1u << 31,
        ARCH_IDR1 = (ARCH_IDR1_SIDSIZE << ARCH_IDR1_SIDSIZE_SHIFT) |
                    (ARCH_IDR1_SSIDSIZE << ARCH_IDR1_SSIDSIZE_SHIFT) |
                    (ARCH_IDR1_QUEUE_LOG2_MAX << ARCH_IDR1_PRIQS_SHIFT) |
                    (ARCH_IDR1_QUEUE_LOG2_MAX << ARCH_IDR1_EVENTQS_SHIFT) |
                    (ARCH_IDR1_QUEUE_LOG2_MAX << ARCH_IDR1_CMDQS_SHIFT) |
                    ARCH_IDR1_ATTR_PERMS_OVR | ARCH_IDR1_ATTR_TYPES_OVR,
        ARCH_S_IDR0_MSI = 1u << 13,
        ARCH_S_IDR0_STALL_MODEL_MASK = 0x3u << 24,
        ARCH_S_IDR0_STALL_MODEL_TERMINATE_ONLY = 0x1u << 24,
        ARCH_S_IDR0_ECMDQ = 1u << 31,
        ARCH_S_IDR0 = ARCH_S_IDR0_MSI |
                      ARCH_S_IDR0_STALL_MODEL_TERMINATE_ONLY,
        ARCH_S_IDR1_SECURE_IMPL = 1u << 31,
        ARCH_S_IDR1_SEL2 = 1u << 29,
        ARCH_S_IDR1_S_SIDSIZE_MASK = 0x3f,
        ARCH_S_IDR1_S_SIDSIZE = ARCH_IDR1_SIDSIZE,
        ARCH_S_IDR1 = ARCH_S_IDR1_SECURE_IMPL | ARCH_S_IDR1_SEL2 |
                      ARCH_S_IDR1_S_SIDSIZE,
        ARCH_IDR2 = 0x00000000,
        ARCH_IDR3_HAD = 1u << 2,
        ARCH_IDR3_XNX = 1u << 4,
        ARCH_IDR3_PPS = 1u << 5,
        ARCH_IDR3_MPAM = 1u << 7,
        ARCH_IDR3_FWB = 1u << 8,
        ARCH_IDR3_STT = 1u << 9,
        ARCH_IDR3_RIL = 1u << 10,
        ARCH_IDR3_BBML_SHIFT = 11,
        ARCH_IDR3_BBML_MASK = 0x3u << ARCH_IDR3_BBML_SHIFT,
        ARCH_IDR3_BBML_LEVEL_1 = 0x1u << ARCH_IDR3_BBML_SHIFT,
        ARCH_IDR3_BBML_LEVEL_2 = 0x2u << ARCH_IDR3_BBML_SHIFT,
        ARCH_IDR3_E0PD = 1u << 13,
        ARCH_IDR3_PTWNNC = 1u << 14,
        ARCH_IDR3_DPT = 1u << 15,
        ARCH_IDR3 = ARCH_IDR3_HAD | ARCH_IDR3_XNX |
                    ARCH_IDR3_MPAM | ARCH_IDR3_RIL |
                    ARCH_IDR3_FWB | ARCH_IDR3_STT |
                    ARCH_IDR3_BBML_LEVEL_2 | ARCH_IDR3_E0PD |
                    ARCH_IDR3_PTWNNC,
        ARCH_S_IDR3_SAMS = 1u << 6,
        ARCH_S_IDR3 = 0x00000000,
        ARCH_IDR4 = 0x00000000,
        ARCH_S_IDR4 = 0x00000000,
        ARCH_MPAMIDR_PARTID_MAX = 31,
        ARCH_MPAMIDR_PMG_MAX = 7,
        ARCH_MPAMIDR = (ARCH_MPAMIDR_PMG_MAX << 16) | ARCH_MPAMIDR_PARTID_MAX,
        ARCH_IDR5_OAS_MASK = 0x7,
        ARCH_IDR5_OAS_48 = 0x5,
        ARCH_IDR5_GRAN4K = 1u << 4,
        ARCH_IDR5_GRAN16K = 1u << 5,
        ARCH_IDR5_GRAN64K = 1u << 6,
        ARCH_IDR5 = ARCH_IDR5_OAS_48 | ARCH_IDR5_GRAN4K |
                    ARCH_IDR5_GRAN16K | ARCH_IDR5_GRAN64K,
        ARCH_IDR6 = 0x00000000,
        ARCH_IIDR = 0x00000000,
        ARCH_AIDR_SMMUV3_3 = 0x00000003,
        ARCH_AIDR = ARCH_AIDR_SMMUV3_3,
        ARCH_STATUS_READY = 1u << 0,
        ARCH_STATUS_QUEUE_MODEL = 1u << 1,
        ARCH_CR0_SMMUEN = 1u << 0,
        ARCH_CR0_PRIQEN = 1u << 1,
        ARCH_CR0_EVENTQEN = 1u << 2,
        ARCH_CR0_CMDQEN = 1u << 3,
        ARCH_CR0_ATSCHK = 1u << 4,
        ARCH_CR2_E2H = 1u << 0,
        ARCH_CR2_RECINVSID = 1u << 1,
        ARCH_CR2_PTM = 1u << 2,
        ARCH_CR2_REC_CFG_ATS = 1u << 3,
        ARCH_CR2_WRITABLE_MASK =
            ARCH_CR2_E2H | ARCH_CR2_RECINVSID | ARCH_CR2_PTM | ARCH_CR2_REC_CFG_ATS,
        ARCH_MPAM_UPDATE = 1u << 31,
        ARCH_GERROR_CMDQ_ABORT = 1u << 0,
        ARCH_GERROR_QUEUE_OVERFLOW = 1u << 1,
        ARCH_GERROR_EVENTQ_ABORT = 1u << 2,
        ARCH_GERROR_PRIQ_ABORT = 1u << 3,
        ARCH_GERROR_MSI_CMDQ_ABORT = 1u << 4,
        ARCH_GERROR_MSI_EVENTQ_ABORT = 1u << 5,
        ARCH_GERROR_MSI_PRIQ_ABORT = 1u << 6,
        ARCH_GERROR_MSI_GERROR_ABORT = 1u << 7,
        ARCH_GERROR_KNOWN_MASK = ARCH_GERROR_CMDQ_ABORT | ARCH_GERROR_QUEUE_OVERFLOW |
                                  ARCH_GERROR_EVENTQ_ABORT | ARCH_GERROR_PRIQ_ABORT |
                                  ARCH_GERROR_MSI_CMDQ_ABORT |
                                  ARCH_GERROR_MSI_EVENTQ_ABORT |
                                  ARCH_GERROR_MSI_PRIQ_ABORT |
                                  ARCH_GERROR_MSI_GERROR_ABORT,
        ARCH_CMDQ_CONS_RD_MASK = 0x000fffff,
        ARCH_CMDQ_CONS_ERR_SHIFT = 24,
        ARCH_CMDQ_CONS_ERR_MASK = 0x7f,
        ARCH_CMDQ_CERROR_NONE = 0,
        ARCH_CMDQ_CERROR_ILL = 1,
        ARCH_CMDQ_CERROR_ABT = 2,
        ARCH_CMDQ_CERROR_ATC_INV = 3,
        ARCH_CMDQ_CERROR_ATC_INV_SYNC = 3,
        ARCH_IRQ_EVENTQ = 1u << 0,
        ARCH_IRQ_PRIQ = 1u << 1,
        ARCH_IRQ_CMDQ_SYNC = 1u << 2,
        ARCH_IRQ_GERROR = 1u << 3,
        ARCH_IRQ_CTRL_WRITABLE_MASK =
            ARCH_IRQ_EVENTQ | ARCH_IRQ_PRIQ | ARCH_IRQ_CMDQ_SYNC | ARCH_IRQ_GERROR,
        ARCH_MSI_CFG2_WRITABLE_MASK = 0x3f,
        ARCH_CMD_SYNC_CS_SHIFT = 12,
        ARCH_CMD_SYNC_CS_MASK = 0x3,
        ARCH_CMD_SYNC_CS_NONE = 0,
        ARCH_CMD_SYNC_CS_IRQ = 1,
        ARCH_CMD_SYNC_CS_SEV = 2,
        ARCH_CMD_SYNC_CS_RESERVED = 3,
        ARCH_CMD_SYNC_MSIDATA_SHIFT = 32,
        ARCH_CMD_SYNC = 0x46,
        ARCH_CMD_PRI_RESP = 0x41,
        ARCH_CMD_RESUME = 0x44,
        ARCH_CMD_STALL_TERM = 0x45,
        ARCH_CMD_RESUME_RESP_SHIFT = 12,
        ARCH_CMD_RESUME_RESP_MASK = 0x3,
        ARCH_CMD_RESUME_STAG_MASK = 0xffff,
        ARCH_CMD_CFGI_STE = 0x03,
        ARCH_CMD_CFGI_ALL = 0x04,
        ARCH_CMD_CFGI_CD = 0x05,
        ARCH_CMD_CFGI_CD_ALL = 0x06,
        ARCH_CMD_CFGI_VMS_PIDM = 0x07,
        ARCH_CMD_TLBI_NH_ALL = 0x10,
        ARCH_CMD_TLBI_NH_ASID = 0x11,
        ARCH_CMD_TLBI_NH_VA = 0x12,
        ARCH_CMD_TLBI_NH_VAA = 0x13,
        ARCH_CMD_TLBI_EL3_ALL = 0x18,
        ARCH_CMD_TLBI_EL3_VA = 0x1a,
        ARCH_CMD_TLBI_EL2_ALL = 0x20,
        ARCH_CMD_TLBI_EL2_ASID = 0x21,
        ARCH_CMD_TLBI_EL2_VA = 0x22,
        ARCH_CMD_TLBI_EL2_VAA = 0x23,
        ARCH_CMD_TLBI_S12_VMALL = 0x28,
        ARCH_CMD_TLBI_S2_IPA = 0x2a,
        ARCH_CMD_TLBI_NSNH_ALL = 0x30,
        ARCH_CMD_TLBI_S_EL2_ALL = 0x50,
        ARCH_CMD_TLBI_S_EL2_ASID = 0x51,
        ARCH_CMD_TLBI_S_EL2_VA = 0x52,
        ARCH_CMD_TLBI_S_EL2_VAA = 0x53,
        ARCH_CMD_TLBI_S_S12_VMALL = 0x58,
        ARCH_CMD_TLBI_S_S2_IPA = 0x5a,
        ARCH_CMD_TLBI_SNH_ALL = 0x60,
        ARCH_CMD_ATC_INV = 0x40,
        ARCH_CMD_DPTI_ALL = 0x70,
        ARCH_CMD_DPTI_PA = 0x73,
        ARCH_ATS_RESP_SUCCESS = 0,
        ARCH_ATS_RESP_UR = 1,
        ARCH_ATS_RESP_CA = 2,
        ARCH_PRI_RESP_ACCEPT = 0,
        ARCH_PRI_RESP_REJECT = 1,
        ARCH_PRI_RESP_FAILURE = 0xf,
        ARCH_CMD_RESUME_RESP_TERM = 0,
        ARCH_CMD_RESUME_RESP_RETRY = 1,
        ARCH_CMD_RESUME_RESP_ABORT = 2,
        ARCH_ENDPOINT_REPLAY_BLOCKING_ENABLE = 0x1,
        ARCH_ENDPOINT_REPLAY_EARLY_RETRY = 0x2,
        ARCH_RESUME_TERMINATE = ARCH_CMD_RESUME_RESP_TERM,
        ARCH_RESUME_RETRY = ARCH_CMD_RESUME_RESP_RETRY,
        ARCH_RESUME_ABORT = ARCH_CMD_RESUME_RESP_ABORT,
        ARCH_EVENT_STAG_MASK = 0xffff,
        ARCH_EVENT_STALL = 1u << 31,
        ARCH_EVENT_PNU_SHIFT = 33,
        ARCH_EVENT_IND_SHIFT = 34,
        ARCH_EVENT_RNW_SHIFT = 35,
        ARCH_EVENT_NSIPA_SHIFT = 38,
        ARCH_EVENT_S2_SHIFT = 39,
        ARCH_EVENT_CLASS_SHIFT = 40,
        ARCH_EVENT_CLASS_MASK = 0x3,
        ARCH_EVENT_GPCF_SHIFT = 16,
        ARCH_EVENT_CLASS_CD = 0,
        ARCH_EVENT_CLASS_TT = 1,
        ARCH_EVENT_CLASS_IN = 2,
        ARCH_EVENT_CLASS_TRANSLATION = ARCH_EVENT_CLASS_IN,
        ARCH_EVENT_SYNTHETIC = 0x01,
        ARCH_EVENT_F_UUT = 0x01,
        ARCH_EVENT_C_BAD_STREAMID = 0x02,
        ARCH_EVENT_F_STE_FETCH = 0x03,
        ARCH_EVENT_C_BAD_STE = 0x04,
        ARCH_EVENT_F_BAD_ATS_TREQ = 0x05,
        ARCH_EVENT_F_STREAM_DISABLED = 0x06,
        ARCH_EVENT_F_TRANSL_FORBIDDEN = 0x07,
        ARCH_EVENT_C_BAD_SUBSTREAMID = 0x08,
        ARCH_EVENT_F_CD_FETCH = 0x09,
        ARCH_EVENT_C_BAD_CD = 0x0a,
        ARCH_EVENT_F_WALK_EABT = 0x0b,
        ARCH_EVENT_F_TRANSLATION = 0x10,
        ARCH_EVENT_F_ADDR_SIZE = 0x11,
        ARCH_EVENT_F_ACCESS = 0x12,
        ARCH_EVENT_F_PERMISSION = 0x13,
        ARCH_EVENT_F_TLB_CONFLICT = 0x20,
        ARCH_EVENT_F_CFG_CONFLICT = 0x21,
        ARCH_EVENT_F_VMS_FETCH = 0x25,
        ARCH_FAULT_CLASS_NONE = 0,
        ARCH_FAULT_CLASS_STE = 1,
        ARCH_FAULT_CLASS_CD = 2,
        ARCH_FAULT_CLASS_TRANSLATION = 3,
        ARCH_FAULT_STAGE_NONE = 0,
        ARCH_FAULT_STAGE_S1 = 1,
        ARCH_FAULT_STAGE_S2 = 2,
        ARCH_FAULT_ATTR_WRITE = 1u << 0,
        ARCH_FAULT_ATTR_STALL = 1u << 1,
        ARCH_FAULT_ATTR_TERMINATE = 1u << 2,
        ARCH_GRANULE_4K = 0,
        ARCH_GRANULE_16K = 1,
        ARCH_GRANULE_64K = 2,
        ARCH_GRANULE_SHIFT = 14,
        ARCH_GRANULE_MASK = 0x3,
        ARCH_START_LEVEL_SHIFT = 16,
        ARCH_START_LEVEL_MASK = 0x3,
        ARCH_STRTAB_FMT_LINEAR = 0,
        ARCH_STRTAB_FMT_2LVL = 1,
        ARCH_STRTAB_CFG_LOG2SIZE_MASK = 0x3f,
        ARCH_STRTAB_CFG_SPLIT_SHIFT = 6,
        ARCH_STRTAB_CFG_SPLIT_MASK = 0x1f,
        ARCH_STRTAB_CFG_FMT_SHIFT = 16,
        ARCH_STRTAB_CFG_FMT_MASK = 0x3,
        ARCH_CMDQ_SSEC = 1ULL << 10,
        ARCH_CMDQ_SSV = 1ULL << 11,
        ARCH_CMDQ_SSID_SHIFT = 12,
        ARCH_CMDQ_SSID_MASK = 0xfffff,
        ARCH_CMDQ_RANGE_NUM_SHIFT = 12,
        ARCH_CMDQ_RANGE_NUM_MASK = 0x1f,
        ARCH_CMDQ_RANGE_SCALE_SHIFT = 20,
        ARCH_CMDQ_RANGE_SCALE_MASK = 0x3f,
        ARCH_CMDQ_RANGE_TG_SHIFT = 10,
        ARCH_CMDQ_RANGE_TG_MASK = 0x3,
        ARCH_CMDQ_RANGE_TG_4K = 1,
        ARCH_CMDQ_RANGE_TG_16K = 2,
        ARCH_CMDQ_RANGE_TG_64K = 3,
        ARCH_CMDQ_TTL_SHIFT = 8,
        ARCH_CMDQ_TTL_MASK = 0x3,
        ARCH_CMDQ_LEAF = 1ULL << 0,
        ARCH_CMDQ_TLBI_VMID_SHIFT = 32,
        ARCH_CMDQ_TLBI_ASID_SHIFT = 48,
        ARCH_CMDQ_TAG_MASK = 0xffff,
        ARCH_TLBI_REGIME_NSNH = 0,
        ARCH_TLBI_REGIME_EL2 = 1,
        ARCH_TLBI_REGIME_EL3 = 2,
        ARCH_TLBI_REGIME_S2 = 3,
        ARCH_TLBI_REGIME_S_EL2 = 4,
    };

    static constexpr uint64_t ARCH_MSI_CFG0_ADDR_MASK = 0x00fffffffffffffcULL;
    static constexpr uint64_t ARCH_EVENT_CONFLICT_REASON_TLB_TAG_MISMATCH =
        apollo::smmuv3::apollo_smmu_arch_core::EVENT_CONFLICT_REASON_TLB_TAG_MISMATCH;
    static constexpr uint64_t ARCH_EVENT_CONFLICT_REASON_CFG_STE_CONT =
        apollo::smmuv3::apollo_smmu_arch_core::EVENT_CONFLICT_REASON_CFG_STE_CONT;

    static constexpr uint64_t ARCH_STE0_MODELED_MASK =
        ARCH_STE_VALID |
        (static_cast<uint64_t>(ARCH_STE_CFG_MASK) << ARCH_STE_CFG_SHIFT) |
        ARCH_STE_S1CTXPTR_MASK |
        (static_cast<uint64_t>(ARCH_STE_S1FMT_MASK) << ARCH_STE_S1FMT_SHIFT) |
        (static_cast<uint64_t>(ARCH_GRANULE_MASK) << ARCH_GRANULE_SHIFT) |
        (static_cast<uint64_t>(ARCH_START_LEVEL_MASK) << ARCH_START_LEVEL_SHIFT) |
        (static_cast<uint64_t>(ARCH_STE_S1CDMAX_MASK) << ARCH_STE_S1CDMAX_SHIFT);
    static constexpr uint64_t ARCH_STE_EATS_FIELD =
        static_cast<uint64_t>(ARCH_STE_EATS_MASK) << ARCH_STE_EATS_SHIFT;
    static constexpr uint64_t ARCH_STE_WORD1_COMPAT_ADDR_MASK =
        ARCH_DESC_OUTPUT_MASK & ~ARCH_STE_EATS_FIELD & ~ARCH_STE_S1MPAM;
    static constexpr uint64_t ARCH_STE1_MODELED_MASK =
        ARCH_STE_S1DSS_MASK | ARCH_STE_S2TTB_MASK | ARCH_DESC_OUTPUT_MASK |
        ARCH_STE_EATS_FIELD | ARCH_STE_S2S | ARCH_STE_S2R;
    static constexpr uint64_t ARCH_STE2_S2VMID_FIELD =
        static_cast<uint64_t>(ARCH_STE_S2VMID_MASK) << ARCH_STE_S2VMID_SHIFT;
    static constexpr uint64_t ARCH_STE2_MODELED_MASK =
        ARCH_STE_S2TTB_MASK | ARCH_STE2_S2VMID_FIELD |
        ARCH_STE_S2PTW | ARCH_STE_S2HA | ARCH_STE_S2HD |
        ARCH_STE_S2HAFT;
    static constexpr uint64_t ARCH_CD0_MODELED_MASK =
        ARCH_CD_VALID | ARCH_CD_VALID_ARCHITECTED |
        (static_cast<uint64_t>(ARCH_GRANULE_MASK) << ARCH_GRANULE_SHIFT) |
        (static_cast<uint64_t>(ARCH_START_LEVEL_MASK) << ARCH_START_LEVEL_SHIFT) |
        ARCH_CD_HA | ARCH_CD_HD | ARCH_CD_ASID_MASK;
    static constexpr uint64_t ARCH_CD1_MODELED_MASK =
        ARCH_CD_TTB0_MASK | ARCH_CD_NSCFG0 | ARCH_CD_HAD0 |
        ARCH_CD_E0PD0 | ARCH_CD_HAFT;
    static constexpr uint64_t ARCH_CD2_MODELED_MASK = ARCH_CD_HAD1 | ARCH_CD_E0PD1;
    static constexpr uint64_t ARCH_CD_L1_DESC_MODELED_MASK =
        ARCH_CD_L1_DESC_VALID | ARCH_CD_L1_DESC_L2PTR_MASK;

    struct map_entry {
        uint64_t iova_base = 0;
        uint64_t pa_base = 0;
        uint64_t size = 0;
        uint32_t stream_id = 0;
        bool valid = false;
    };

    struct ats_entry {
        uint32_t stream_id = 0;
        uint64_t page = 0;
        uint16_t asid = 0;
        uint16_t vmid = 0;
        uint32_t ssid = 0;
        uint32_t leaf_level = ARCH_LEVELS - 1;
        uint64_t granule_bytes = PAGE_SIZE;
        uint8_t security_state = ARCH_SECURITY_NONSECURE;
        uint8_t tlbi_regime = ARCH_TLBI_REGIME_NSNH;
        bool ssid_valid = false;
        bool valid = false;
    };

    struct arch_config_cache_entry {
        uint32_t stream_base = 0;
        uint32_t stream_span = 1;
        uint32_t security_state = ARCH_SECURITY_NONSECURE;
        uint64_t ste0 = 0;
        uint64_t ste1 = 0;
        uint64_t ste2 = 0;
        bool valid = false;
    };

    using arch_queue = apollo::smmuv3::apollo_smmu_arch_core::queue_state;
    using arch_stream_context_state =
        apollo::smmuv3::apollo_smmu_arch_core::stream_context_descriptor_state;
    using arch_walker_state = apollo::smmuv3::apollo_smmu_arch_core::page_table_walker_state;
    using arch_fault_replay_state =
        apollo::smmuv3::apollo_smmu_arch_core::fault_replay_state;

    struct arch_msi_config {
        uint64_t addr = 0;
        uint32_t data = 0;
        uint32_t attr = 0;
    };

    struct arch_pri_request {
        uint32_t stream_id = 0;
        uint16_t prg = 0;
        uint8_t ats_status = ARCH_ATS_RESP_SUCCESS;
        uint8_t security_state = ARCH_SECURITY_NONSECURE;
        uint64_t iova = 0;
        uint32_t ssid = 0;
        bool ssid_valid = false;
        bool pending = false;
    };

    using arch_stall_record = apollo::smmuv3::apollo_smmu_arch_core::stall_record;
    using arch_endpoint_replay_record =
        apollo::smmuv3::apollo_smmu_arch_core::endpoint_replay_record;
    using arch_endpoint_replay_transaction =
        apollo::smmuv3::apollo_smmu_arch_core::endpoint_replay_transaction;
    using arch_endpoint_replay_table =
        apollo::smmuv3::apollo_smmu_arch_core::endpoint_replay_table;
    using arch_descriptor_memory_read =
        apollo::smmuv3::apollo_smmu_arch_core::descriptor_memory_read;

    class arch_io_executor {
    public:
        virtual ~arch_io_executor() = default;

        virtual bool read_descriptor(apollo_smmu_tbu& tbu,
                                     const arch_descriptor_memory_read& read,
                                     uint64_t& desc) = 0;
        virtual tlm::tlm_response_status replay_transaction(
            apollo_smmu_tbu& tbu,
            const arch_endpoint_replay_transaction& transaction,
            sc_core::sc_time& delay) = 0;
    };

    class default_arch_io_executor : public arch_io_executor {
    public:
        bool read_descriptor(apollo_smmu_tbu& tbu,
                             const arch_descriptor_memory_read& read,
                             uint64_t& desc) override;
        tlm::tlm_response_status replay_transaction(
            apollo_smmu_tbu& tbu,
            const arch_endpoint_replay_transaction& transaction,
            sc_core::sc_time& delay) override;
    };

    struct arch_walk_config {
        uint32_t granule = ARCH_GRANULE_4K;
        uint32_t start_level = 0;
        uint32_t levels = ARCH_LEVELS;
        uint32_t stage = ARCH_FAULT_STAGE_S1;
    };

    std::array<map_entry, 64> m_maps {};
    std::array<ats_entry, 32> m_ats_cache {};
    std::array<arch_config_cache_entry, 16> m_arch_config_cache {};
    std::array<arch_pri_request, 16> m_arch_pri_pending {};
    apollo::smmuv3::apollo_smmu_arch_core m_arch_core {};
    default_arch_io_executor m_default_arch_io_executor {};
    arch_io_executor* m_arch_io_executor = &m_default_arch_io_executor;
    apollo::smmuv3::apollo_smmu_arch_core::stall_record_table& m_arch_stalls;
    arch_endpoint_replay_table& m_arch_endpoint_replays;
    arch_queue& m_cmdq;
    arch_queue& m_eventq;
    arch_queue& m_priq;
    arch_stream_context_state& m_arch_stream_context;
    arch_walker_state& m_arch_walker;
    arch_fault_replay_state& m_arch_fault_replay;
    uint64_t m_map_iova = 0;
    uint64_t m_map_pa = 0;
    uint64_t m_map_size = 0;
    uint64_t m_last_fault_iova = 0;
    uint32_t m_map_stream_id = 0;
    bool m_map_stream_id_valid = false;
    uint64_t& m_arch_ttbr;
    uint64_t& m_arch_ste_base;
    uint64_t& m_arch_cd_base;
    uint64_t& m_arch_iova;
    uint64_t& m_arch_last_ipa;
    uint64_t& m_arch_last_fetch_addr;
    uint64_t& m_arch_s2ttb;
    uint64_t m_arch_last_s_s2ttb = 0;
    uint32_t m_arch_last_nscfg = ARCH_STE_NSCFG_USE_INCOMING;
    bool m_arch_last_s2_secure_ipa = false;
    bool m_arch_last_cd_nscfg0 = false;
    bool m_arch_last_s1_table_walk_nonsecure = true;
    bool m_arch_last_s1_output_nonsecure_ipa = true;
    bool m_arch_last_s1_tt_fetch_secure_ipa = false;
    uint64_t m_arch_last_s1_tt_fetch_s2ttb = 0;
    uint64_t& m_arch_last_desc;
    uint64_t& m_arch_last_pa;
	    uint64_t m_arch_last_par = 0;
	    uint32_t m_arch_gatos_ctrl = 0;
	    uint64_t m_arch_gatos_sid = 0;
	    uint64_t m_arch_gatos_addr = 0;
	    uint64_t m_arch_gatos_par = 0;
	    uint32_t m_arch_vatos_ctrl = 0;
	    uint64_t m_arch_vatos_sid = 0;
	    uint64_t m_arch_vatos_addr = 0;
	    uint64_t m_arch_vatos_par = 0;
	    uint32_t m_arch_vatos_sel = 0;
	    bool m_arch_atos_request_active = false;
	    uint32_t m_arch_atos_type = ARCH_ATOS_ADDR_TYPE_STAGE1_STAGE2;
	    bool m_arch_atos_virtual_interface = false;
    bool m_arch_translated_split_stage2_request_active = false;
    bool m_arch_ats_treq_split_stage2_request_active = false;
    bool m_arch_translated_effective_access_valid = false;
    bool m_arch_translated_effective_privileged = false;
    bool m_arch_translated_effective_instruction = false;
    bool m_arch_current_access_privileged = false;
    bool m_arch_current_access_instruction = false;
    bool m_arch_current_cd_ha = false;
    bool m_arch_current_cd_hd = false;
    bool m_arch_current_cd_haft = false;
    bool m_arch_current_s2_ptw = false;
    bool m_arch_current_s2_ha = false;
    bool m_arch_current_s2_hd = false;
    bool m_arch_current_s2_haft = false;
    bool m_arch_last_split_stage_in_privileged = false;
    bool m_arch_last_split_stage_in_instruction = false;
    bool m_arch_last_split_stage_effective_privileged = false;
    bool m_arch_last_split_stage_effective_instruction = false;
    bool m_arch_last_atos_privileged = false;
    bool m_arch_last_atos_instruction = false;
    bool m_arch_last_atos_read = true;
    bool m_arch_last_atos_ste_attrs_ignored = false;
    bool m_arch_last_had0 = false;
    bool m_arch_last_had1 = false;
    bool m_arch_last_had_disabled_hier_attrs = false;
    bool m_arch_last_hier_attrs_applied = false;
    bool m_arch_last_xnx_fault = false;
    bool m_arch_last_bbml2_nt_ignored = false;
    uint64_t m_arch_last_bbml2_nt_desc = 0;
    bool m_arch_last_httu_af_update = false;
    bool m_arch_last_httu_dirty_update = false;
    bool m_arch_last_httu_table_af_update = false;
    bool m_arch_last_ats_treq_write = false;
    uint64_t m_arch_last_httu_desc_pa = 0;
    uint64_t m_arch_last_httu_desc_before = 0;
    uint64_t m_arch_last_httu_desc_after = 0;
	    uint32_t m_arch_secure_gatos_ctrl = 0;
	    uint64_t m_arch_secure_gatos_sid = 0;
	    uint64_t m_arch_secure_gatos_addr = 0;
	    uint64_t m_arch_secure_gatos_par = 0;
	    uint32_t m_arch_secure_vatos_ctrl = 0;
	    uint64_t m_arch_secure_vatos_sid = 0;
	    uint64_t m_arch_secure_vatos_addr = 0;
	    uint64_t m_arch_secure_vatos_par = 0;
	    uint32_t m_arch_secure_vatos_sel = 0;
	    uint64_t m_arch_last_ste = 0;
    uint64_t m_arch_last_ste1 = 0;
    uint64_t m_arch_last_ste4 = 0;
    uint64_t m_arch_last_ste5 = 0;
    uint64_t m_arch_last_vms_ptr = 0;
    std::array<uint64_t, ARCH_VMS_PARTID_MAP_WORDS> m_arch_last_vms_partid_map {};
    uint64_t m_arch_last_cd = 0;
    uint64_t m_arch_last_cd2 = 0;
    uint64_t m_arch_last_cd5 = 0;
    bool m_arch_last_e0pd_fault = false;
    bool m_arch_last_ptwnnc_device_fetch = false;
    bool m_arch_last_ptwnnc_normalized = false;
    bool m_arch_last_s2ptw_fault = false;
    bool m_arch_last_mpam_valid = false;
    bool m_arch_last_mpam_remapped = false;
    bool m_arch_last_mpam_unknown = false;
    uint8_t m_arch_last_mpam_partid_space = ARCH_MPAM_SPACE_NONSECURE;
    uint16_t m_arch_last_mpam_partid = 0;
    uint16_t m_arch_last_mpam_virtual_partid = 0;
    uint8_t m_arch_last_mpam_pmg = 0;
    bool m_arch_last_output_attrs_valid = false;
    bool m_arch_last_output_mtcfg = false;
    uint8_t m_arch_last_output_mem_type = 0;
    uint8_t m_arch_last_output_shareability = 0;
    uint8_t m_arch_last_output_alloc_hint = 0;
    uint8_t m_arch_last_output_inst_cfg = 0;
    uint8_t m_arch_last_output_priv_cfg = 0;
    uint8_t m_arch_last_output_ns_cfg = 0;
    uint8_t m_arch_last_security_state = ARCH_SECURITY_NONSECURE;
    bool m_arch_last_security_supported = true;
    uint8_t m_arch_last_event_security_state = ARCH_SECURITY_NONSECURE;
    uint32_t m_arch_eventq_nonsecure_records = 0;
    uint32_t m_arch_eventq_secure_records = 0;
    uint32_t m_arch_eventq_realm_records = 0;
    uint32_t m_arch_eventq_root_records = 0;
    uint32_t m_arch_stall_model = ARCH_STALL_MODEL_STALL;
    bool m_arch_fault_stage2_stall = false;
    struct arch_security_eventq_bank {
        arch_queue queue {};
        std::array<uint64_t, 4> last_record {};
        uint64_t last_guest_record_addr = 0;
        uint32_t last_prod = 0;
        uint32_t last_cons = 0;
        uint32_t records = 0;
        bool queue_configured = false;
        bool routed_to_separate_queue = false;
        bool valid = false;
    };
    std::array<arch_security_eventq_bank, 4> m_arch_security_eventq_banks {};
    struct arch_security_strtab_bank {
        uint64_t base = 0;
        uint32_t cfg = 0;
        uint64_t last_ste_pa = 0;
        uint32_t last_stream_id = 0;
        uint32_t lookups = 0;
        bool configured = false;
        bool valid = false;
    };
    std::array<arch_security_strtab_bank, 4> m_arch_security_strtab_banks {};
    struct arch_stall_event_buffer_record {
        std::array<uint64_t, 4> words {};
        uint8_t security_state = ARCH_SECURITY_NONSECURE;
    };
    uint32_t& m_arch_stream_id;
    bool& m_arch_stream_id_valid;
    uint32_t& m_arch_walk_depth;
    uint32_t& m_arch_fault_reason;
    uint32_t& m_arch_fault_stage;
    uint32_t& m_arch_fault_event_class;
    bool& m_arch_fault_nsipa;
    bool& m_arch_fault_gpcf;
    bool& m_arch_fault_record_suppressed;
    uint32_t& m_arch_last_stage;
    uint32_t& m_arch_fault_replays;
    uint32_t& m_arch_ats_responses;
    uint32_t& m_arch_pri_responses;
    uint32_t& m_arch_ats_success;
    uint32_t& m_arch_ats_ur;
    uint32_t& m_arch_ats_ca;
    uint32_t& m_arch_pri_accepted;
    uint32_t& m_arch_pri_rejected;
    uint32_t& m_arch_pri_unknown;
    uint32_t& m_arch_pri_auto_responses;
    uint32_t& m_arch_pri_auto_failures;
    uint32_t m_arch_pri_stop_markers = 0;
    uint32_t m_arch_pri_discarded_nonlast = 0;
    uint32_t m_arch_pri_secure_auto_failures = 0;
    uint32_t m_arch_pri_auto_ste_ppar_checks = 0;
    uint32_t m_arch_pri_auto_ste_ppar_failures = 0;
    uint32_t m_arch_pri_ppar_lookup_fault_records = 0;
    uint64_t m_arch_last_pri_ppr_word0 = 0;
    uint64_t m_arch_last_pri_ppr_word3 = 0;
    bool m_arch_last_pri_ppar = false;
    bool m_arch_last_pri_pps = false;
    bool m_arch_last_auto_response_ssv = false;
    uint32_t m_arch_last_auto_response_ssid = 0;
    uint32_t m_arch_msi_writes = 0;
    uint32_t m_arch_msi_aborts = 0;
    uint64_t m_arch_last_msi_addr = 0;
    uint32_t m_arch_last_msi_data = 0;
    uint32_t m_arch_last_msi_source = 0;
    uint32_t m_arch_last_cmd_sync_signal = ARCH_CMD_SYNC_CS_NONE;
    uint32_t m_arch_cmd_processed = 0;
    uint32_t m_arch_cmd_syncs = 0;
    uint32_t m_arch_cmd_cfgis = 0;
    uint32_t m_arch_cmd_tlbis = 0;
    uint32_t m_arch_cmd_atc_invs = 0;
    uint32_t m_arch_cmd_dptis = 0;
    uint32_t m_arch_cmd_invalidations = 0;
    uint32_t m_arch_cmd_table_invalidations = 0;
    uint32_t m_arch_tlb_conflict_recoveries = 0;
    uint32_t m_arch_cfg_conflict_recoveries = 0;
    uint32_t m_arch_last_cmd_opcode = 0;
    uint32_t m_arch_last_cmd_stream_id = 0;
    uint16_t m_arch_last_cmd_asid = 0;
    uint16_t m_arch_last_cmd_vmid = 0;
    uint32_t m_arch_last_cmd_ssid = 0;
    bool m_arch_last_cmd_ssid_valid = false;
    uint32_t m_arch_last_cmd_invalidated = 0;
    uint8_t m_arch_last_cmd_security_state = ARCH_SECURITY_NONSECURE;
    bool m_arch_last_cmd_ssec = false;
    uint64_t m_arch_last_cmd_iova = 0;
    uint64_t m_arch_last_cmd_range_bytes = 0;
    uint32_t m_arch_last_cmd_table_invalidated = 0;
    uint32_t m_arch_last_cmd_tg = 0;
    uint32_t m_arch_last_cmd_ttl = 0;
    bool m_arch_last_cmd_leaf = false;
    uint16_t m_arch_current_asid = 0;
    uint16_t m_arch_current_vmid = 0;
    uint32_t m_arch_current_ssid = 0;
    bool m_arch_current_ssid_valid = false;
    uint32_t& m_arch_selected_ssid;
    bool& m_arch_selected_ssid_valid;
    uint16_t m_arch_last_asid = 0;
    uint16_t m_arch_last_vmid = 0;
    uint32_t m_arch_last_cd_ssid = 0;
    uint32_t m_arch_last_s1cdmax = 0;
    uint32_t m_arch_last_s1dss = 0;
    uint32_t m_arch_last_s1fmt = 0;
    bool m_arch_last_cd_l2 = false;
    bool m_arch_last_cd_bypass = false;
    uint32_t m_arch_last_eats = ARCH_STE_EATS_DISABLED;
    uint64_t m_arch_last_cd_pa = 0;
    uint32_t& m_arch_last_fault_detail;
    uint32_t& m_arch_stall_pending;
    uint32_t& m_arch_stall_retried;
    uint32_t& m_arch_stall_terminated;
    uint32_t& m_arch_stall_buffered;
    uint32_t& m_arch_stall_redriven;
    uint32_t& m_arch_stall_suppressed;
    uint32_t& m_arch_stall_merged;
    uint32_t& m_arch_resume_unknown;
    uint32_t& m_arch_endpoint_replay_pending;
    uint32_t& m_arch_endpoint_replay_retried;
    uint32_t& m_arch_endpoint_replay_succeeded;
    uint32_t& m_arch_endpoint_replay_terminated;
    uint32_t& m_arch_endpoint_replay_redriven;
    uint32_t& m_arch_endpoint_replay_failed;
    uint32_t& m_arch_endpoint_block_waits;
    uint32_t& m_arch_endpoint_block_resumed;
    uint32_t& m_arch_endpoint_block_failed;
    bool& m_arch_endpoint_blocking_enabled;
    sc_core::sc_event m_arch_endpoint_replay_resume_event;
    uint32_t& m_arch_early_retry_attempted;
    uint32_t& m_arch_early_retry_succeeded;
    uint32_t& m_arch_early_retry_failed;
    uint32_t& m_arch_early_retry_discarded;
    uint32_t& m_arch_next_fault_replay_id;
    uint16_t& m_arch_next_stag;
    uint16_t& m_arch_last_stag;
    uint16_t& m_arch_last_resume_stag;
    uint16_t& m_arch_next_prg;
    uint16_t& m_arch_last_prg;
    uint16_t& m_arch_last_auto_prg;
    uint8_t& m_arch_last_auto_response;
    uint32_t m_arch_cr0 = 0;
    uint32_t m_arch_cr0ack = 0;
    uint32_t m_arch_cr1 = 0;
    uint32_t m_arch_cr2 = 0;
    uint32_t m_arch_gbpa = 0;
    uint32_t m_arch_gmpam = 0;
    uint32_t m_arch_gbpmpam = 0;
    uint32_t m_arch_secure_cr0 = 0;
    uint32_t m_arch_secure_cr0ack = 0;
    uint32_t m_arch_secure_cr1 = 0;
    uint32_t m_arch_secure_cr2 = 0;
    uint32_t m_arch_secure_gbpa = 0;
    uint32_t m_arch_secure_gmpam = 0;
    uint32_t m_arch_secure_gbpmpam = 0;
    uint32_t m_arch_secure_irq_ctrl = 0;
    uint32_t m_arch_secure_irq_ctrlack = 0;
    uint32_t m_arch_secure_gerror = 0;
    uint32_t m_arch_secure_gerrorn = 0;
    arch_queue m_arch_secure_cmdq {};
    arch_queue m_arch_secure_priq {};
    uint32_t m_arch_secure_cmdq_cerror = ARCH_CMDQ_CERROR_NONE;
    uint32_t m_arch_irq_ctrl = 0;
    uint32_t m_arch_irq_ctrlack = 0;
    uint32_t m_arch_irq_status = 0;
    uint32_t m_arch_irq_lines = 0;
    uint32_t m_arch_gerror = 0;
    uint32_t m_arch_gerrorn = 0;
    uint8_t m_arch_eventq_irq_security_state = ARCH_SECURITY_NONSECURE;
    uint8_t m_arch_priq_irq_security_state = ARCH_SECURITY_NONSECURE;
    uint8_t m_arch_cmdq_sync_irq_security_state = ARCH_SECURITY_NONSECURE;
    uint8_t m_arch_gerror_irq_security_state = ARCH_SECURITY_NONSECURE;
    arch_msi_config m_arch_gerror_msi {};
    arch_msi_config m_arch_secure_gerror_msi {};
    arch_msi_config m_arch_eventq_msi {};
    arch_msi_config m_arch_secure_eventq_msi {};
    arch_msi_config m_arch_priq_msi {};
    arch_msi_config m_arch_secure_priq_msi {};
    uint32_t m_arch_cmdq_cerror = ARCH_CMDQ_CERROR_NONE;
    uint32_t m_arch_atc_inv_sync_errors = 0;
    uint32_t m_arch_atc_inv_sync_pending_count = 0;
    uint32_t m_arch_atc_inv_sync_force_fail_count = 0;
    uint32_t& m_arch_strtab_cfg;
    uint64_t& m_arch_strtab_base;
    uint32_t m_map_status = MAP_STATUS_IDLE;
    uint32_t m_arch_status = ARCH_STATUS_IDLE;
    uint32_t m_ats_entries = 0;
    uint32_t m_ats_fills = 0;
    uint32_t m_pri_requests = 0;
    uint32_t m_fault_count = 0;
    bool m_dynamic_enabled = false;
    bool m_arch_atc_inv_sync_error_pending = false;
    bool m_arch_atc_inv_sync_force_fail = false;
    std::deque<arch_stall_event_buffer_record> m_arch_stall_event_buffer;

    uint32_t default_stream_id() const
    {
        return p_stream_id.get_value();
    }

    uint32_t selected_map_stream_id() const
    {
        return m_map_stream_id_valid ? m_map_stream_id : default_stream_id();
    }

    uint32_t selected_arch_stream_id() const
    {
        return m_arch_stream_id_valid ? m_arch_stream_id : default_stream_id();
    }

    uint32_t transaction_stream_id(const tlm::tlm_generic_payload& trans) const
    {
        auto* ext = trans.get_extension<gs::ApolloSmmuStreamIdExtension>();
        return ext ? ext->stream_id : default_stream_id();
    }

    bool transaction_substream_id(const tlm::tlm_generic_payload& trans, uint32_t& ssid) const
    {
        auto* ext = trans.get_extension<gs::ApolloSmmuStreamIdExtension>();
        if (ext == nullptr || !ext->substream_id_valid) {
            ssid = 0;
            return false;
        }

        ssid = ext->substream_id & ARCH_CMDQ_SSID_MASK;
        return true;
    }

    bool transaction_privileged(const tlm::tlm_generic_payload& trans) const
    {
        auto* ext = trans.get_extension<gs::ApolloSmmuStreamIdExtension>();
        return ext != nullptr && ext->privileged;
    }

    bool transaction_instruction(const tlm::tlm_generic_payload& trans) const
    {
        auto* ext = trans.get_extension<gs::ApolloSmmuStreamIdExtension>();
        return ext != nullptr && ext->instruction;
    }

    bool transaction_translated(const tlm::tlm_generic_payload& trans) const
    {
        auto* ext = trans.get_extension<gs::ApolloSmmuStreamIdExtension>();
        return ext != nullptr && ext->translated;
    }

    uint8_t transaction_security_state(const tlm::tlm_generic_payload& trans) const
    {
        auto* ext = trans.get_extension<gs::ApolloSmmuStreamIdExtension>();
        return ext != nullptr ? ext->security_state : ARCH_SECURITY_NONSECURE;
    }

    static bool arch_security_state_supported(uint8_t security_state)
    {
        return security_state == ARCH_SECURITY_NONSECURE ||
               security_state == ARCH_SECURITY_SECURE ||
               security_state == ARCH_SECURITY_REALM ||
               security_state == ARCH_SECURITY_ROOT;
    }

    uint32_t arch_security_status() const
    {
        return (static_cast<uint32_t>(m_arch_last_security_state) & 0x3u) |
               (m_arch_last_security_supported ? (1u << 8) : 0u) |
               ((static_cast<uint32_t>(m_arch_last_event_security_state) &
                 ARCH_SECURITY_EVENTQ_STATE_MASK) << ARCH_SECURITY_EVENTQ_STATE_SHIFT);
    }

    uint32_t arch_security_eventq_count(uint8_t security_state) const
    {
        switch (security_state) {
        case ARCH_SECURITY_NONSECURE:
            return m_arch_eventq_nonsecure_records;
        case ARCH_SECURITY_SECURE:
            return m_arch_eventq_secure_records;
        case ARCH_SECURITY_REALM:
            return m_arch_eventq_realm_records;
        case ARCH_SECURITY_ROOT:
            return m_arch_eventq_root_records;
        default:
            return 0;
        }
    }

    const arch_security_eventq_bank& arch_security_eventq_bank_state(
        uint8_t security_state) const
    {
        return m_arch_security_eventq_banks[security_state &
                                           ARCH_SECURITY_EVENTQ_STATE_MASK];
    }

    static uint8_t arch_security_eventq_index(uint8_t security_state)
    {
        return security_state & ARCH_SECURITY_EVENTQ_STATE_MASK;
    }

    static bool arch_security_uses_secure_irq_bank(uint8_t security_state)
    {
        return arch_security_eventq_index(security_state) == ARCH_SECURITY_SECURE;
    }

    bool arch_security_eventq_uses_separate_bank(uint8_t security_state) const
    {
        const uint8_t state = arch_security_eventq_index(security_state);

        return state != ARCH_SECURITY_NONSECURE &&
               m_arch_security_eventq_banks[state].queue_configured;
    }

    arch_queue& arch_eventq_for_security_state(uint8_t security_state)
    {
        const uint8_t state = arch_security_eventq_index(security_state);

        if (state != ARCH_SECURITY_NONSECURE &&
            m_arch_security_eventq_banks[state].queue_configured) {
            return m_arch_security_eventq_banks[state].queue;
        }

        return m_eventq;
    }

    const arch_queue& arch_eventq_for_security_state(uint8_t security_state) const
    {
        const uint8_t state = arch_security_eventq_index(security_state);

        if (state != ARCH_SECURITY_NONSECURE &&
            m_arch_security_eventq_banks[state].queue_configured) {
            return m_arch_security_eventq_banks[state].queue;
        }

        return m_eventq;
    }

    arch_queue& arch_priq_for_security_state(uint8_t security_state)
    {
        if (arch_security_uses_secure_irq_bank(security_state)) {
            return m_arch_secure_priq;
        }

        return m_priq;
    }

    const arch_queue& arch_priq_for_security_state(uint8_t security_state) const
    {
        if (arch_security_uses_secure_irq_bank(security_state)) {
            return m_arch_secure_priq;
        }

        return m_priq;
    }

    void configure_arch_security_eventq_bank(uint8_t security_state, uint64_t base,
                                             uint32_t prod = 0, uint32_t cons = 0)
    {
        const uint8_t state = arch_security_eventq_index(security_state);
        auto& bank = m_arch_security_eventq_banks[state];

        bank.queue.base = base;
        bank.queue.prod = prod & ARCH_QUEUE_INDEX_MASK;
        bank.queue.cons = cons & ARCH_QUEUE_INDEX_MASK;
        bank.queue.overflow = false;
        bank.queue.ovflg = false;
        bank.queue.ovackflg = false;
        bank.queue_configured = state != ARCH_SECURITY_NONSECURE;
        bank.routed_to_separate_queue = false;
    }

    const arch_security_strtab_bank& arch_security_strtab_bank_state(
        uint8_t security_state) const
    {
        return m_arch_security_strtab_banks[arch_security_eventq_index(security_state)];
    }

    bool arch_security_strtab_uses_bank(uint8_t security_state) const
    {
        const uint8_t state = arch_security_eventq_index(security_state);

        return state != ARCH_SECURITY_NONSECURE &&
               m_arch_security_strtab_banks[state].configured;
    }

    void configure_arch_security_strtab_bank(uint8_t security_state, uint64_t base,
                                             uint32_t cfg)
    {
        const uint8_t state = arch_security_eventq_index(security_state);
        auto& bank = m_arch_security_strtab_banks[state];

        bank.base = base;
        bank.cfg = cfg;
        bank.last_ste_pa = 0;
        bank.last_stream_id = 0;
        bank.lookups = 0;
        bank.valid = false;
        bank.configured = state != ARCH_SECURITY_NONSECURE;
    }

    bool arch_active_stream_table_configured() const
    {
        return m_arch_strtab_base != 0 ||
               arch_security_strtab_uses_bank(m_arch_last_security_state);
    }

    static uint32_t arch_ste_nscfg(uint64_t ste1)
    {
        return (ste1 >> ARCH_STE_NSCFG_SHIFT) & ARCH_STE_NSCFG_MASK;
    }

    static bool arch_ste_mtcfg(uint64_t ste1)
    {
        return (ste1 & ARCH_STE_MTCFG) != 0;
    }

    static bool arch_ste_ppar(uint64_t ste1)
    {
        return (ste1 & ARCH_STE_PPAR) != 0;
    }

    static uint32_t arch_ste_memattr(uint64_t ste1)
    {
        return (ste1 >> ARCH_STE_MEMATTR_SHIFT) & ARCH_STE_MEMATTR_MASK;
    }

    static uint32_t arch_ste_shcfg(uint64_t ste1)
    {
        return (ste1 >> ARCH_STE_SHCFG_SHIFT) & ARCH_STE_SHCFG_MASK;
    }

    static uint32_t arch_ste_alloccfg(uint64_t ste1)
    {
        return (ste1 >> ARCH_STE_ALLOCCFG_SHIFT) & ARCH_STE_ALLOCCFG_MASK;
    }

    static uint32_t arch_ste_privcfg(uint64_t ste1)
    {
        return (ste1 >> ARCH_STE_PRIVCFG_SHIFT) & ARCH_STE_PRIVCFG_MASK;
    }

    static uint32_t arch_ste_instcfg(uint64_t ste1)
    {
        return (ste1 >> ARCH_STE_INSTCFG_SHIFT) & ARCH_STE_INSTCFG_MASK;
    }

    static bool arch_ste_effective_privileged(uint64_t ste1, bool incoming)
    {
        switch (arch_ste_privcfg(ste1)) {
        case 2:
            return false;
        case 3:
            return true;
        default:
            return incoming;
        }
    }

    static bool arch_ste_effective_instruction(uint64_t ste1, bool incoming,
                                               bool write)
    {
        if (write) {
            return false;
        }

        switch (arch_ste_instcfg(ste1)) {
        case 2:
            return false;
        case 3:
            return true;
        default:
            return incoming;
        }
    }

    static uint32_t arch_gbpa_normalize_2bit(uint32_t value)
    {
        value &= 0x3;
        return value == 1 ? 0 : value;
    }

    static uint32_t arch_gbpa_memattr(uint32_t gbpa)
    {
        return (gbpa >> ARCH_GBPA_MEMATTR_SHIFT) & ARCH_GBPA_MEMATTR_MASK;
    }

    static bool arch_gbpa_mtcfg(uint32_t gbpa)
    {
        return (gbpa & ARCH_GBPA_MTCFG) != 0;
    }

    static uint32_t arch_gbpa_alloccfg(uint32_t gbpa)
    {
        const uint32_t alloccfg =
            (gbpa >> ARCH_GBPA_ALLOCCFG_SHIFT) & ARCH_GBPA_ALLOCCFG_MASK;
        return (alloccfg & 0x8) != 0 ? alloccfg : 0;
    }

    static uint32_t arch_gbpa_shcfg(uint32_t gbpa)
    {
        return (gbpa >> ARCH_GBPA_SHCFG_SHIFT) & ARCH_GBPA_SHCFG_MASK;
    }

    static uint32_t arch_gbpa_privcfg(uint32_t gbpa)
    {
        return arch_gbpa_normalize_2bit(
            (gbpa >> ARCH_GBPA_PRIVCFG_SHIFT) & ARCH_GBPA_PRIVCFG_MASK);
    }

    static uint32_t arch_gbpa_instcfg(uint32_t gbpa, bool write)
    {
        if (write) {
            return 0;
        }

        return arch_gbpa_normalize_2bit(
            (gbpa >> ARCH_GBPA_INSTCFG_SHIFT) & ARCH_GBPA_INSTCFG_MASK);
    }

    static bool arch_gbpa_abort(uint32_t gbpa)
    {
        return (gbpa & ARCH_GBPA_ABORT) != 0;
    }

    static uint32_t arch_gbpa_normalize(uint32_t gbpa)
    {
        uint32_t normalized = gbpa & ARCH_GBPA_KNOWN_MASK;
        const uint32_t instcfg = arch_gbpa_instcfg(normalized, false);
        const uint32_t privcfg = arch_gbpa_privcfg(normalized);

        normalized &= ~((ARCH_GBPA_INSTCFG_MASK << ARCH_GBPA_INSTCFG_SHIFT) |
                        (ARCH_GBPA_PRIVCFG_MASK << ARCH_GBPA_PRIVCFG_SHIFT) |
                        (ARCH_GBPA_ALLOCCFG_MASK << ARCH_GBPA_ALLOCCFG_SHIFT));
        normalized |= instcfg << ARCH_GBPA_INSTCFG_SHIFT;
        normalized |= privcfg << ARCH_GBPA_PRIVCFG_SHIFT;
        normalized |= arch_gbpa_alloccfg(gbpa) << ARCH_GBPA_ALLOCCFG_SHIFT;
        return normalized;
    }

    static bool arch_ste_s2_stall(uint64_t ste1)
    {
        return (ste1 & ARCH_STE_S2S) != 0;
    }

    static bool arch_ste_stage2_enabled(uint64_t ste0)
    {
        if ((ste0 & ARCH_STE_VALID) == 0) {
            return false;
        }

        const uint32_t config = arch_ste_config(ste0);
        return config == ARCH_STE_CFG_S2_TRANS || config == ARCH_STE_CFG_NESTED;
    }

    static bool arch_ste_all_bypass(uint64_t ste0)
    {
        return (ste0 & ARCH_STE_VALID) != 0 &&
               arch_ste_config(ste0) == ARCH_STE_CFG_BYPASS;
    }

    bool arch_stall_model_terminates_stage2_stalls() const
    {
        return m_arch_stall_model == ARCH_STALL_MODEL_TERMINATE_ONLY;
    }

    static bool arch_ste_s2_record(uint64_t ste1)
    {
        return (ste1 & ARCH_STE_S2R) != 0;
    }

    void apply_arch_stage2_fault_policy(uint64_t ste1)
    {
        if (m_arch_fault_stage != ARCH_FAULT_STAGE_S2) {
            return;
        }

        const bool stall = arch_ste_s2_stall(ste1);
        const bool record = arch_ste_s2_record(ste1);

        m_arch_fault_stage2_stall = stall;
        m_arch_fault_record_suppressed = !stall && !record;
    }

    static uint64_t arch_ste_s2ttb(uint64_t ste1, uint64_t ste2)
    {
        const uint64_t ste1_nscfg_field =
            static_cast<uint64_t>(ARCH_STE_NSCFG_MASK) <<
            ARCH_STE_NSCFG_SHIFT;
        const uint64_t ste2_s2ttb = ste2 & ARCH_STE_S2TTB_MASK;

        return ste2_s2ttb != 0 ?
                   ste2_s2ttb :
                   ((ste1 & ARCH_STE_S2TTB_MASK) & ~ste1_nscfg_field);
    }

    static uint16_t arch_ste_s2vmid(uint64_t ste2)
    {
        const uint64_t vmid_word =
            ste2 & ~ARCH_STE_S2PTW & ~ARCH_STE_S2HA &
            ~ARCH_STE_S2HD & ~ARCH_STE_S2HAFT;

        return static_cast<uint16_t>((vmid_word >> ARCH_STE_S2VMID_SHIFT) &
                                     ARCH_STE_S2VMID_MASK);
    }

    uint16_t arch_active_vatos_sel() const
    {
        return arch_security_eventq_index(m_arch_last_security_state) ==
                       ARCH_SECURITY_SECURE ?
                   static_cast<uint16_t>(m_arch_secure_vatos_sel &
                                         ARCH_STE_S2VMID_MASK) :
                   static_cast<uint16_t>(m_arch_vatos_sel &
                                         ARCH_STE_S2VMID_MASK);
    }

    bool arch_validate_vatos_vmid_scope(uint32_t stream_id, uint64_t ste_pa,
                                        uint64_t ste0, uint64_t ste2)
    {
        if (!m_arch_atos_request_active || !m_arch_atos_virtual_interface) {
            return true;
        }

        const uint16_t selected_vmid = arch_active_vatos_sel();
        const uint16_t ste_vmid = arch_ste_s2vmid(ste2);
        if (arch_ste_has_stage2(ste0) && ste_vmid == selected_vmid) {
            return true;
        }

        m_arch_fault_reason = ARCH_FAULT_ATOS_INV_REQ;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_record_suppressed = true;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural VATOS VMID scope reject"
                     << " stream-id=0x" << std::hex << stream_id
                     << " ste-pa=0x" << ste_pa
                     << " selected-vmid=0x" << selected_vmid
                     << " ste-vmid=0x" << ste_vmid
                     << " stage2=" << (arch_ste_has_stage2(ste0) ? 1 : 0)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural VATOS VMID scope reject"
                  << " stream-id=0x" << std::hex << stream_id
                  << " ste-pa=0x" << ste_pa
                  << " selected-vmid=0x" << selected_vmid
                  << " ste-vmid=0x" << ste_vmid
                  << " stage2=" << (arch_ste_has_stage2(ste0) ? 1 : 0)
                  << std::dec << std::endl;
        return false;
    }

    static bool arch_ste_nscfg_outputs_nonsecure(uint32_t nscfg,
                                                 bool incoming_nonsecure)
    {
        switch (nscfg & ARCH_STE_NSCFG_MASK) {
        case ARCH_STE_NSCFG_SECURE:
            return false;
        case ARCH_STE_NSCFG_NONSECURE:
            return true;
        case ARCH_STE_NSCFG_RESERVED:
        case ARCH_STE_NSCFG_USE_INCOMING:
        default:
            return incoming_nonsecure;
        }
    }

    static bool arch_cd_nscfg0(uint64_t cd1)
    {
        return (cd1 & ARCH_CD_NSCFG0) != 0;
    }

    static bool arch_cd_had0(uint64_t cd1)
    {
        return (cd1 & ARCH_CD_HAD0) != 0;
    }

    static bool arch_cd_had1(uint64_t cd2)
    {
        return (cd2 & ARCH_CD_HAD1) != 0;
    }

    static bool arch_cd_ha(uint64_t cd0)
    {
        return (cd0 & ARCH_CD_HA) != 0;
    }

    static bool arch_cd_hd(uint64_t cd0)
    {
        return (cd0 & ARCH_CD_HD) != 0;
    }

    static bool arch_cd_haft(uint64_t cd1)
    {
        return (cd1 & ARCH_CD_HAFT) != 0;
    }

    static bool arch_cd_e0pd0(uint64_t cd1)
    {
        return (cd1 & ARCH_CD_E0PD0) != 0;
    }

    static bool arch_cd_e0pd1(uint64_t cd2)
    {
        return (cd2 & ARCH_CD_E0PD1) != 0;
    }

    static bool arch_va_selects_ttb1(uint64_t iova)
    {
        return (iova & (1ULL << 55)) != 0;
    }

    static bool arch_table_desc_has_hier_attrs(uint64_t desc)
    {
        return (desc & (ARCH_DESC_APTABLE_NO_UNPRIV | ARCH_DESC_APTABLE_RO |
                        ARCH_DESC_UXNTABLE | ARCH_DESC_PXNTABLE)) != 0;
    }

    static uint32_t arch_desc_s2_memattr(uint64_t desc)
    {
        return static_cast<uint32_t>((desc >> ARCH_DESC_S2_MEMATTR_SHIFT) &
                                     ARCH_DESC_S2_MEMATTR_MASK);
    }

    static bool arch_desc_s2_memattr_is_device(uint64_t desc)
    {
        return arch_desc_s2_memattr(desc) < ARCH_DESC_S2_MEMATTR_NORMAL_NC;
    }

    bool arch_s2ptw_reject_device_fetch(uint32_t stream_id,
                                        const char* fetch_kind,
                                        uint64_t fetch_ipa,
                                        uint64_t fetch_pa,
                                        uint64_t s2_desc,
                                        uint32_t event_class)
    {
        if (!m_arch_current_s2_ptw ||
            !arch_desc_s2_memattr_is_device(s2_desc)) {
            return false;
        }

        m_arch_last_ptwnnc_device_fetch = true;
        m_arch_last_s2ptw_fault = true;
        m_arch_fault_reason = ARCH_FAULT_PERMISSION;
        m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
        m_arch_fault_event_class = event_class;
        m_arch_last_ipa = fetch_ipa;
        SCP_WARN(()) << "APOLLO_SMMU_TBU: STE.S2PTW blocks Device-mapped "
                     << fetch_kind
                     << " stream-id=0x" << std::hex << stream_id
                     << " ipa=0x" << fetch_ipa
                     << " pa=0x" << fetch_pa
                     << " s2-desc=0x" << s2_desc << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: STE.S2PTW blocks Device-mapped "
                  << fetch_kind
                  << " stream-id=0x" << std::hex << stream_id
                  << " ipa=0x" << fetch_ipa
                  << " pa=0x" << fetch_pa
                  << " s2-desc=0x" << s2_desc
                  << std::dec << std::endl;
        return true;
    }

    static bool arch_ste_s2ha(uint64_t ste2)
    {
        return (ste2 & ARCH_STE_S2HA) != 0;
    }

    static bool arch_ste_s2hd(uint64_t ste2)
    {
        return (ste2 & ARCH_STE_S2HD) != 0;
    }

    static bool arch_ste_s2ptw(uint64_t ste2)
    {
        return (ste2 & ARCH_STE_S2PTW) != 0;
    }

    static bool arch_ste_s2haft(uint64_t ste2)
    {
        return (ste2 & ARCH_STE_S2HAFT) != 0;
    }

    static bool arch_desc_is_leaf_candidate(const arch_walk_config& cfg,
                                            uint32_t level, uint64_t desc)
    {
        if (level + 1 < cfg.levels) {
            return (desc & ARCH_DESC_TYPE_MASK) == ARCH_DESC_BLOCK;
        }

        return (desc & ARCH_DESC_TYPE_MASK) == ARCH_DESC_PAGE;
    }

    static bool arch_desc_is_table_candidate(const arch_walk_config& cfg,
                                             uint32_t level, uint64_t desc)
    {
        return level + 1 < cfg.levels &&
               (desc & ARCH_DESC_TYPE_MASK) == ARCH_DESC_TABLE;
    }

    bool arch_httu_access_enabled(const arch_walk_config& cfg) const
    {
        if ((ARCH_IDR0 & ARCH_IDR0_HTTU_MASK) == 0) {
            return false;
        }

        if (cfg.stage == ARCH_FAULT_STAGE_S1) {
            return m_arch_current_cd_ha;
        }
        if (cfg.stage == ARCH_FAULT_STAGE_S2) {
            return m_arch_current_s2_ha;
        }

        return false;
    }

    bool arch_httu_table_access_enabled(const arch_walk_config& cfg) const
    {
        if ((ARCH_IDR0 & ARCH_IDR0_HTTU_MASK) !=
            ARCH_IDR0_HTTU_ACCESS_DIRTY_TABLE) {
            return false;
        }

        if (cfg.stage == ARCH_FAULT_STAGE_S1) {
            return m_arch_current_cd_ha && m_arch_current_cd_haft;
        }
        if (cfg.stage == ARCH_FAULT_STAGE_S2) {
            return m_arch_current_s2_ha && m_arch_current_s2_haft;
        }

        return false;
    }

    bool arch_httu_dirty_enabled(const arch_walk_config& cfg) const
    {
        const uint32_t httu = ARCH_IDR0 & ARCH_IDR0_HTTU_MASK;

        if (httu != ARCH_IDR0_HTTU_ACCESS_DIRTY &&
            httu != ARCH_IDR0_HTTU_ACCESS_DIRTY_TABLE) {
            return false;
        }

        if (cfg.stage == ARCH_FAULT_STAGE_S1) {
            return m_arch_current_cd_ha && m_arch_current_cd_hd;
        }
        if (cfg.stage == ARCH_FAULT_STAGE_S2) {
            return m_arch_current_s2_ha && m_arch_current_s2_hd;
        }

        return false;
    }

    bool arch_apply_httu_table_update(uint32_t stream_id, uint64_t desc_pa,
                                      uint64_t desc_fetch_pa, uint64_t& desc,
                                      const arch_walk_config& cfg,
                                      uint32_t level)
    {
        if (!arch_desc_is_table_candidate(cfg, level, desc) ||
            (desc & ARCH_DESC_AF) != 0 ||
            !arch_httu_table_access_enabled(cfg)) {
            return true;
        }

        const uint64_t updated = desc | ARCH_DESC_AF;
        if (!write_downstream_u64(desc_fetch_pa, updated)) {
            m_arch_fault_reason = ARCH_FAULT_WALK_EABT;
            m_arch_fault_stage = cfg.stage;
            return false;
        }

        m_arch_last_httu_af_update = true;
        m_arch_last_httu_table_af_update = true;
        m_arch_last_httu_desc_pa = desc_pa;
        m_arch_last_httu_desc_before = desc;
        m_arch_last_httu_desc_after = updated;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: HTTU table descriptor AF update"
                     << " stream-id=0x" << std::hex << stream_id
                     << " stage=" << std::dec << cfg.stage
                     << " level=" << level
                     << " desc-pa=0x" << std::hex << desc_pa
                     << " fetch-pa=0x" << desc_fetch_pa
                     << " before=0x" << desc
                     << " after=0x" << updated
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: HTTU table descriptor AF update"
                  << " stream-id=0x" << std::hex << stream_id
                  << " stage=" << std::dec << cfg.stage
                  << " level=" << level
                  << " desc-pa=0x" << std::hex << desc_pa
                  << " fetch-pa=0x" << desc_fetch_pa
                  << " before=0x" << desc
                  << " after=0x" << updated
                  << std::dec << std::endl;
        desc = updated;
        return true;
    }

    bool arch_apply_httu_leaf_update(uint32_t stream_id, uint64_t desc_pa,
                                     uint64_t desc_fetch_pa, uint64_t& desc,
                                     const arch_walk_config& cfg,
                                     uint32_t level, bool write)
    {
        if (!arch_desc_is_leaf_candidate(cfg, level, desc)) {
            return true;
        }

        uint64_t updated = desc;
        bool access_update = false;
        bool dirty_update = false;

        if ((updated & ARCH_DESC_AF) == 0 && arch_httu_access_enabled(cfg)) {
            updated |= ARCH_DESC_AF;
            access_update = true;
        }
        if (write && (updated & ARCH_DESC_AP_RO) != 0 &&
            (updated & ARCH_DESC_DBM) != 0 && arch_httu_dirty_enabled(cfg)) {
            updated &= ~ARCH_DESC_AP_RO;
            dirty_update = true;
        }
        if (updated == desc) {
            return true;
        }
        if (!write_downstream_u64(desc_fetch_pa, updated)) {
            m_arch_fault_reason = ARCH_FAULT_WALK_EABT;
            m_arch_fault_stage = cfg.stage;
            return false;
        }

        m_arch_last_httu_af_update |= access_update;
        m_arch_last_httu_dirty_update |= dirty_update;
        m_arch_last_httu_desc_pa = desc_pa;
        m_arch_last_httu_desc_before = desc;
        m_arch_last_httu_desc_after = updated;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: HTTU descriptor update"
                     << " stream-id=0x" << std::hex << stream_id
                     << " stage=" << std::dec << cfg.stage
                     << " level=" << level
                     << " desc-pa=0x" << std::hex << desc_pa
                     << " fetch-pa=0x" << desc_fetch_pa
                     << " before=0x" << desc
                     << " after=0x" << updated
                     << " af=" << (access_update ? 1 : 0)
                     << " dirty=" << (dirty_update ? 1 : 0)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: HTTU descriptor update"
                  << " stream-id=0x" << std::hex << stream_id
                  << " stage=" << std::dec << cfg.stage
                  << " level=" << level
                  << " desc-pa=0x" << std::hex << desc_pa
                  << " fetch-pa=0x" << desc_fetch_pa
                  << " before=0x" << desc
                  << " after=0x" << updated
                  << " af=" << (access_update ? 1 : 0)
                  << " dirty=" << (dirty_update ? 1 : 0)
                  << std::dec << std::endl;
        desc = updated;
        return true;
    }

    bool arch_stage1_access_privileged() const
    {
        if (m_arch_atos_request_active) {
            return m_arch_last_atos_privileged;
        }
        if (m_arch_translated_effective_access_valid) {
            return m_arch_translated_effective_privileged;
        }
        return m_arch_current_access_privileged;
    }

    bool arch_access_instruction() const
    {
        if (m_arch_atos_request_active) {
            return m_arch_last_atos_instruction;
        }
        if (m_arch_translated_effective_access_valid) {
            return m_arch_translated_effective_instruction;
        }
        return m_arch_current_access_instruction;
    }

    bool arch_cd_had_disables_hier_attrs(uint64_t iova, uint64_t cd1,
                                         uint64_t cd2) const
    {
        return arch_va_selects_ttb1(iova) ? arch_cd_had1(cd2) :
                                            arch_cd_had0(cd1);
    }

    bool arch_cd_e0pd_blocks_access(uint64_t iova, uint64_t cd1,
                                    uint64_t cd2) const
    {
        if (arch_stage1_access_privileged()) {
            return false;
        }
        return arch_va_selects_ttb1(iova) ? arch_cd_e0pd1(cd2) :
                                            arch_cd_e0pd0(cd1);
    }

    bool read_arch_ste_s_s2ttb(uint32_t stream_id, uint64_t ste_pa,
                               uint64_t& s_s2ttb)
    {
        uint64_t ste6 = 0;

        if (!read_downstream_u64(ste_pa + ARCH_STE_S_S2TTB_WORD_OFFSET,
                                 ste6, true)) {
            set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S2,
                                 ste_pa + ARCH_STE_S_S2TTB_WORD_OFFSET);
            return false;
        }

        s_s2ttb = ste6 & ARCH_STE_S_S2TTB_MASK;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural S_S2TTB read"
                     << " stream-id=0x" << std::hex << stream_id
                     << " ste-pa=0x" << ste_pa
                     << " s_s2ttb=0x" << s_s2ttb << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural S_S2TTB read"
                  << " stream-id=0x" << std::hex << stream_id
                  << " ste-pa=0x" << ste_pa
                  << " s_s2ttb=0x" << s_s2ttb << std::dec << std::endl;
        return true;
    }

    bool select_arch_stage2_table_base(uint32_t stream_id, uint64_t ste_pa,
                                       uint64_t ste1, uint64_t ste2,
                                       bool stage1_bypassed,
                                       bool incoming_nonsecure_ipa,
                                       uint64_t& s2ttb)
    {
        const uint8_t security_state =
            arch_security_eventq_index(m_arch_last_security_state);

        m_arch_last_nscfg = arch_ste_nscfg(ste1);
        m_arch_last_s_s2ttb = 0;
        m_arch_last_s2_secure_ipa = false;

        const bool secure_stage2_parallel = security_state == ARCH_SECURITY_SECURE;
        const bool nonsecure_ipa =
            stage1_bypassed ?
                arch_ste_nscfg_outputs_nonsecure(m_arch_last_nscfg,
                                                  incoming_nonsecure_ipa) :
                incoming_nonsecure_ipa;

        if (secure_stage2_parallel && !nonsecure_ipa) {
            if (!read_arch_ste_s_s2ttb(stream_id, ste_pa,
                                       m_arch_last_s_s2ttb)) {
                return false;
            }

            m_arch_last_s2_secure_ipa = true;
            s2ttb = m_arch_last_s_s2ttb;
        } else {
            s2ttb = arch_ste_s2ttb(ste1, ste2);
        }
        m_arch_fault_nsipa = secure_stage2_parallel && nonsecure_ipa;

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural stage-2 table select"
                     << " stream-id=0x" << std::hex << stream_id
                     << " ste-pa=0x" << ste_pa
                     << " security-state=0x" << static_cast<uint32_t>(security_state)
                     << " nscfg=0x" << m_arch_last_nscfg
                     << " incoming-nsipa=" << (incoming_nonsecure_ipa ? 1 : 0)
                     << " secure-ipa=" << (m_arch_last_s2_secure_ipa ? 1 : 0)
                     << " s2ttb=0x" << s2ttb << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural stage-2 table select"
                  << " stream-id=0x" << std::hex << stream_id
                  << " ste-pa=0x" << ste_pa
                  << " security-state=0x" << static_cast<uint32_t>(security_state)
                  << " nscfg=0x" << m_arch_last_nscfg
                  << " incoming-nsipa=" << (incoming_nonsecure_ipa ? 1 : 0)
                  << " secure-ipa=" << (m_arch_last_s2_secure_ipa ? 1 : 0)
                  << " s2ttb=0x" << s2ttb << std::dec << std::endl;
        return true;
    }

    void record_arch_eventq_security_route(uint8_t security_state,
                                           const std::array<uint64_t, 4>& words,
                                           uint64_t guest_record_addr,
                                           uint32_t prod, uint32_t cons)
    {
        m_arch_last_event_security_state =
            security_state & ARCH_SECURITY_EVENTQ_STATE_MASK;
        auto& bank = m_arch_security_eventq_banks[m_arch_last_event_security_state];
        bank.last_record = words;
        bank.last_guest_record_addr = guest_record_addr;
        bank.last_prod = prod;
        bank.last_cons = cons;
        bank.records++;
        bank.routed_to_separate_queue =
            arch_security_eventq_uses_separate_bank(m_arch_last_event_security_state);
        bank.valid = true;

        switch (m_arch_last_event_security_state) {
        case ARCH_SECURITY_NONSECURE:
            m_arch_eventq_nonsecure_records++;
            break;
        case ARCH_SECURITY_SECURE:
            m_arch_eventq_secure_records++;
            break;
        case ARCH_SECURITY_REALM:
            m_arch_eventq_realm_records++;
            break;
        case ARCH_SECURITY_ROOT:
            m_arch_eventq_root_records++;
            break;
        default:
            break;
        }
    }

    bool translate_segment(uint32_t stream_id, uint64_t iova, uint64_t len, uint64_t& pa,
                           uint64_t& segment_len, bool ssid_valid = false, uint32_t ssid = 0,
                           bool write = false, bool preserve_mpam_state = false,
                           bool preserve_output_attrs_state = false)
    {
        if (!preserve_mpam_state) {
            reset_arch_mpam_state();
        }
        if (!preserve_output_attrs_state) {
            reset_arch_output_attrs();
        }
        const bool smmu_enabled =
            arch_smmu_enabled_for_security_state(m_arch_last_security_state);
        if (!smmu_enabled) {
            const uint32_t gbpa = arch_gbpa_for_security_state(m_arch_last_security_state);
            if (arch_gbpa_abort(gbpa)) {
                m_arch_fault_reason = ARCH_FAULT_NONE;
                m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
                m_arch_fault_record_suppressed = true;
                m_arch_status = ARCH_STATUS_ERROR;
                SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural GBPA abort"
                             << " stream-id=0x" << std::hex << stream_id
                             << " iova=0x" << iova << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architectural GBPA abort"
                          << " stream-id=0x" << std::hex << stream_id
                          << " iova=0x" << iova << std::dec << std::endl;
                return false;
            }
            if (!arch_translated_addr_in_oas(iova)) {
                m_arch_fault_reason = ARCH_FAULT_ADDR_SIZE;
                m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
                m_arch_fault_record_suppressed = true;
                m_arch_status = ARCH_STATUS_ERROR;
                SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural GBPA OAS abort"
                             << " stream-id=0x" << std::hex << stream_id
                             << " iova=0x" << iova
                             << " mask=0x" << arch_translated_addr_mask()
                             << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architectural GBPA OAS abort"
                          << " stream-id=0x" << std::hex << stream_id
                          << " iova=0x" << iova
                          << " mask=0x" << arch_translated_addr_mask()
                          << std::dec << std::endl;
                return false;
            }
            record_arch_mpam_from_gbp(stream_id);
            record_arch_gbpa_output_attrs(stream_id, gbpa, "global-bypass", write);
        }

        if (!m_dynamic_enabled && smmu_enabled &&
            (m_arch_ste_base != 0 || m_arch_strtab_base != 0)) {
            uint64_t desc = 0;

            if (!arch_stream_context_walk(stream_id, iova, pa, desc, write)) {
                return false;
            }
            segment_len = std::min(len, page_remaining(iova));
            return segment_len != 0;
        }

        if (m_dynamic_enabled) {
            for (const auto& map : m_maps) {
                if (!map.valid || map.stream_id != stream_id || len == 0 || iova < map.iova_base) {
                    continue;
                }

                const uint64_t offset = iova - map.iova_base;
                if (offset < map.size) {
                    pa = map.pa_base + offset;
                    segment_len = std::min({len, map.size - offset, page_remaining(iova)});
                    record_page_walk(stream_id, iova, pa, segment_len, ssid_valid, ssid);
                    return true;
                }
            }

            return false;
        }

        if (stream_id != default_stream_id()) {
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
        record_page_walk(stream_id, iova, pa, segment_len, ssid_valid, ssid);
        return true;
    }

    bool translate_segment(uint64_t iova, uint64_t len, uint64_t& pa, uint64_t& segment_len)
    {
        return translate_segment(default_stream_id(), iova, len, pa, segment_len);
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

    static uint64_t arch_translated_addr_mask()
    {
        return ARCH_DESC_OUTPUT_MASK | (PAGE_SIZE - 1);
    }

    static bool arch_translated_addr_in_oas(uint64_t addr)
    {
        return (addr & ~arch_translated_addr_mask()) == 0;
    }

    static bool arch_translated_split_stage_supported(uint32_t ste_cfg)
    {
        /*
         * Split-stage ATS Translated traffic carries an IPA.  The modeled
         * QBox endpoint path therefore must route it through stage 2 only:
         * the architected Nested STE case bypasses stage 1 for AT=Translated,
         * while the existing stage-2-only stream case is kept as a bounded
         * compatibility slice for earlier Apollo tests.
         */
        return ste_cfg == ARCH_STE_CFG_S2_TRANS ||
               ste_cfg == ARCH_STE_CFG_NESTED;
    }

    static bool arch_translated_dpt_supported()
    {
        return arch_dpt_supported();
    }

    static bool arch_dpt_supported()
    {
        return (ARCH_IDR3 & ARCH_IDR3_DPT) != 0;
    }

    static bool arch_pasidtt_supported()
    {
        return false;
    }

    static bool arch_translated_eats_unsupported_by_protocol(uint32_t eats,
                                                             uint32_t ste_cfg)
    {
        return eats == ARCH_STE_EATS_SPLIT &&
               !arch_translated_split_stage_supported(ste_cfg);
    }

    static bool arch_translated_eats_unsupported_by_dpt(uint32_t eats)
    {
        return eats == ARCH_STE_EATS_DPT && !arch_translated_dpt_supported();
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

    uint32_t map_count(uint32_t stream_id) const
    {
        uint32_t count = 0;

        for (const auto& map : m_maps) {
            if (map.valid && map.stream_id == stream_id) {
                count++;
            }
        }

        return count;
    }

    void add_map()
    {
        const uint32_t stream_id = selected_map_stream_id();

        if (m_map_size == 0) {
            m_map_status = MAP_STATUS_ERROR;
            log_map_error("map", stream_id, m_map_iova, m_map_pa, m_map_size);
            return;
        }

        m_dynamic_enabled = true;
        for (auto& map : m_maps) {
            if (map.valid && map.stream_id == stream_id && map.iova_base == m_map_iova) {
                map.pa_base = m_map_pa;
                map.size = m_map_size;
                m_map_status = MAP_STATUS_OK;
                clear_ats_cache(stream_id);
                log_map("remap", stream_id, m_map_iova, m_map_pa, m_map_size);
                log_pri_resolved(stream_id, m_map_iova, m_map_pa, m_map_size);
                return;
            }
        }

        for (auto& map : m_maps) {
            if (!map.valid) {
                map.iova_base = m_map_iova;
                map.pa_base = m_map_pa;
                map.size = m_map_size;
                map.stream_id = stream_id;
                map.valid = true;
                m_map_status = MAP_STATUS_OK;
                clear_ats_cache(stream_id);
                log_map("map", stream_id, m_map_iova, m_map_pa, m_map_size);
                log_pri_resolved(stream_id, m_map_iova, m_map_pa, m_map_size);
                return;
            }
        }

        m_map_status = MAP_STATUS_ERROR;
        log_map_error("map-full", stream_id, m_map_iova, m_map_pa, m_map_size);
    }

    void remove_map()
    {
        const uint32_t stream_id = selected_map_stream_id();
        bool removed = false;

        m_dynamic_enabled = true;
        for (auto& map : m_maps) {
            if (!map.valid || map.stream_id != stream_id || map.iova_base != m_map_iova) {
                continue;
            }
            map.valid = false;
            removed = true;
        }

        m_map_status = removed ? MAP_STATUS_OK : MAP_STATUS_ERROR;
        if (removed) {
            clear_ats_cache(stream_id);
            log_map("unmap", stream_id, m_map_iova, m_map_pa, m_map_size);
        } else {
            log_map_error("unmap-miss", stream_id, m_map_iova, m_map_pa, m_map_size);
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
        clear_config_cache();
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
               FEATURE_CONTEXT_DESCRIPTOR_WALK | FEATURE_ARCH_FAULT_REPLAY | FEATURE_ARCH_ATS_PRI_PROTOCOL |
               FEATURE_ARCH_REG_QUEUE_SURFACE | FEATURE_MULTI_STREAM_ID | FEATURE_ARCH_CMD_INVALIDATION |
               FEATURE_ARCH_TAGGED_INVALIDATION | FEATURE_ARCH_CD_TABLE_INDEX |
               FEATURE_ARCH_RESERVED_ENCODING_CHECKS | FEATURE_ARCH_EVENT_RECORD_LAYOUT |
               FEATURE_ENDPOINT_SUBSTREAM_ID | FEATURE_ARCH_CR0_QUEUE_GATES |
        FEATURE_ARCH_ATSCHK_EATS_GATES | FEATURE_ARCH_REC_CFG_ATS_GATES |
        FEATURE_ARCH_DPTI_UNSUPPORTED | FEATURE_ARCH_CMDQ_CERROR |
        FEATURE_ARCH_QUEUE_OVERFLOW_FLAGS | FEATURE_ARCH_STALL_BUFFER_REDRIVE |
        FEATURE_ARCH_PRI_AUTO_RESPONSE | FEATURE_ARCH_IRQ_MSI_CFG |
        FEATURE_ARCH_CONFIG_DISABLED_NO_EVENT | FEATURE_ARCH_F_UUT_EVENT |
        FEATURE_ARCH_VMS_FETCH | FEATURE_ARCH_CFGI_VMS_PIDM;
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

    static bool ats_security_matches(const ats_entry& entry, uint8_t security_state)
    {
        return security_state == ARCH_SECURITY_ANY ||
               entry.security_state ==
                   (security_state & ARCH_SECURITY_EVENTQ_STATE_MASK);
    }

    uint32_t clear_ats_cache(uint8_t security_state = ARCH_SECURITY_ANY)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && ats_security_matches(entry, security_state)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache(uint32_t stream_id)
    {
        return clear_ats_cache(stream_id, ARCH_SECURITY_ANY);
    }

    uint32_t clear_ats_cache(uint32_t stream_id, uint8_t security_state)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && entry.stream_id == stream_id &&
                ats_security_matches(entry, security_state)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    static bool ats_tlbi_regime_is_nsnh(const ats_entry& entry)
    {
        return entry.tlbi_regime == ARCH_TLBI_REGIME_NSNH;
    }

    uint32_t clear_ats_cache_nsnh(uint8_t security_state)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && ats_security_matches(entry, security_state) &&
                ats_tlbi_regime_is_nsnh(entry)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache_page(uint64_t page,
                                  uint8_t security_state = ARCH_SECURITY_ANY)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && entry.page == page &&
                ats_security_matches(entry, security_state)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache_page(uint32_t stream_id, uint64_t page)
    {
        return clear_ats_cache_page(stream_id, page, ARCH_SECURITY_ANY);
    }

    uint32_t clear_ats_cache_page(uint32_t stream_id, uint64_t page,
                                  uint8_t security_state)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && entry.stream_id == stream_id && entry.page == page &&
                ats_security_matches(entry, security_state)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache_vmid(uint16_t vmid,
                                  uint8_t security_state = ARCH_SECURITY_ANY)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && entry.vmid == vmid &&
                ats_security_matches(entry, security_state)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache_asid(uint16_t asid, uint16_t vmid,
                                  uint8_t security_state = ARCH_SECURITY_ANY)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && entry.asid == asid && entry.vmid == vmid &&
                ats_security_matches(entry, security_state)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache_page_asid(uint64_t page, uint16_t asid,
                                       uint16_t vmid,
                                       uint8_t security_state = ARCH_SECURITY_ANY)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && entry.page == page && entry.asid == asid &&
                entry.vmid == vmid && ats_security_matches(entry, security_state)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache_page_vmid(uint64_t page, uint16_t vmid,
                                       uint8_t security_state = ARCH_SECURITY_ANY)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && entry.page == page && entry.vmid == vmid &&
                ats_security_matches(entry, security_state)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    static bool ats_page_in_range(uint64_t page, uint64_t base,
                                  uint64_t range_bytes)
    {
        if (range_bytes == 0) {
            return page == base;
        }
        if (UINT64_MAX - base < range_bytes - 1) {
            return page >= base;
        }
        return page >= base && page < base + range_bytes;
    }

    static bool ats_tlbi_level_matches(const ats_entry& entry, uint32_t ttl,
                                       uint32_t tg)
    {
        if (tg != 0 && entry.granule_bytes != cmdq_range_granule_bytes(tg)) {
            return false;
        }
        return ttl == 0 || entry.leaf_level == ttl;
    }

    uint32_t clear_ats_cache_range_asid(uint64_t page, uint64_t range_bytes,
                                        uint16_t asid, uint16_t vmid,
                                        uint8_t security_state = ARCH_SECURITY_ANY,
                                        uint32_t ttl = 0, uint32_t tg = 0)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && ats_page_in_range(entry.page, page, range_bytes) &&
                entry.asid == asid && entry.vmid == vmid &&
                ats_security_matches(entry, security_state) &&
                ats_tlbi_level_matches(entry, ttl, tg)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache_range_vmid(uint64_t page, uint64_t range_bytes,
                                        uint16_t vmid,
                                        uint8_t security_state = ARCH_SECURITY_ANY,
                                        uint32_t ttl = 0, uint32_t tg = 0)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && ats_page_in_range(entry.page, page, range_bytes) &&
                entry.vmid == vmid &&
                ats_security_matches(entry, security_state) &&
                ats_tlbi_level_matches(entry, ttl, tg)) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    uint32_t clear_ats_cache_page_ssid(uint32_t stream_id, uint64_t page,
                                       bool ssid_valid, uint32_t ssid,
                                       uint8_t security_state = ARCH_SECURITY_ANY)
    {
        uint32_t entries = 0;
        uint32_t removed = 0;

        for (auto& entry : m_ats_cache) {
            if (entry.valid && entry.stream_id == stream_id && entry.page == page &&
                ats_security_matches(entry, security_state) &&
                (!ssid_valid || (entry.ssid_valid && entry.ssid == ssid))) {
                entry.valid = false;
                removed++;
            }
            if (entry.valid) {
                entries++;
            }
        }
        m_ats_entries = entries;
        return removed;
    }

    bool ats_lookup(uint32_t stream_id, uint64_t page,
                    uint8_t security_state = ARCH_SECURITY_ANY) const
    {
        for (const auto& entry : m_ats_cache) {
            if (entry.valid && entry.stream_id == stream_id && entry.page == page &&
                ats_security_matches(entry, security_state)) {
                return true;
            }
        }
        return false;
    }

    bool ats_lookup(uint32_t stream_id, uint64_t page, uint16_t asid, uint16_t vmid,
                    bool ssid_valid = false, uint32_t ssid = 0,
                    uint8_t security_state = ARCH_SECURITY_ANY) const
    {
        for (const auto& entry : m_ats_cache) {
            if (entry.valid && entry.stream_id == stream_id && entry.page == page &&
                entry.asid == asid && entry.vmid == vmid &&
                ats_security_matches(entry, security_state) &&
                entry.ssid_valid == ssid_valid && (!ssid_valid || entry.ssid == ssid)) {
                return true;
            }
        }
        return false;
    }

    static bool ats_tags_match(const ats_entry& entry, uint16_t asid, uint16_t vmid,
                               bool ssid_valid, uint32_t ssid)
    {
        return entry.asid == asid && entry.vmid == vmid &&
               entry.ssid_valid == ssid_valid && (!ssid_valid || entry.ssid == ssid);
    }

    bool ats_cache_conflict_present(uint32_t stream_id, uint64_t page, uint16_t asid,
                                    uint16_t vmid, bool ssid_valid = false,
                                    uint32_t ssid = 0,
                                    uint8_t security_state = ARCH_SECURITY_ANY) const
    {
        for (const auto& entry : m_ats_cache) {
            if (entry.valid && entry.stream_id == stream_id && entry.page == page &&
                ats_security_matches(entry, security_state) &&
                !ats_tags_match(entry, asid, vmid, ssid_valid, ssid)) {
                return true;
            }
        }
        return false;
    }

    bool record_ats_cache_conflict_if_present(uint32_t stream_id, uint64_t iova,
                                              uint16_t asid, uint16_t vmid,
                                              bool ssid_valid = false,
                                              uint32_t ssid = 0,
                                              uint8_t security_state = ARCH_SECURITY_ANY)
    {
        const uint64_t page = page_base(iova);

        if (!ats_cache_conflict_present(stream_id, page, asid, vmid, ssid_valid, ssid,
                                        security_state)) {
            return false;
        }

        m_arch_fault_reason = ARCH_FAULT_TLB_CONFLICT;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        record_fault(stream_id, "tlb-conflict", iova, PAGE_SIZE, false, ssid_valid,
                     ssid);
        m_arch_tlb_conflict_recoveries +=
            clear_ats_cache_page(stream_id, page, security_state);
        return true;
    }

    static uint32_t config_cache_span_entries(uint32_t span_log2)
    {
        /*
         * The modeled span follows the STE.CONT encoding shape: CONT==0 covers
         * one STE and larger values cover 2^CONT StreamIDs.  The architectural
         * field is four bits wide, so cap external test inputs to the largest
         * representable span rather than allowing a shift overflow.
         */
        const uint32_t capped = std::min<uint32_t>(span_log2, 15);
        return 1u << capped;
    }

    static uint32_t config_cache_span_base(uint32_t stream_id, uint32_t span_log2)
    {
        const uint32_t span = config_cache_span_entries(span_log2);

        return stream_id & ~(span - 1u);
    }

    static bool config_cache_spans_overlap(uint32_t base_a, uint32_t span_a,
                                           uint32_t base_b, uint32_t span_b)
    {
        const uint64_t end_a = static_cast<uint64_t>(base_a) + span_a;
        const uint64_t end_b = static_cast<uint64_t>(base_b) + span_b;

        return static_cast<uint64_t>(base_a) < end_b &&
               static_cast<uint64_t>(base_b) < end_a;
    }

    static bool config_cache_ste_matches(const arch_config_cache_entry& entry,
                                         uint64_t ste0, uint64_t ste1,
                                         uint64_t ste2)
    {
        return entry.ste0 == ste0 && entry.ste1 == ste1 && entry.ste2 == ste2;
    }

    uint32_t clear_config_cache()
    {
        uint32_t removed = 0;

        for (auto& entry : m_arch_config_cache) {
            if (entry.valid) {
                entry = arch_config_cache_entry {};
                removed++;
            }
        }
        return removed;
    }

    uint32_t clear_config_cache_security_state(uint32_t security_state)
    {
        uint32_t removed = 0;

        for (auto& entry : m_arch_config_cache) {
            if (entry.valid && entry.security_state == security_state) {
                entry = arch_config_cache_entry {};
                removed++;
            }
        }
        return removed;
    }

    uint32_t clear_config_cache_span(uint32_t stream_id, uint32_t security_state,
                                     uint32_t span_log2)
    {
        const uint32_t base = config_cache_span_base(stream_id, span_log2);
        const uint32_t span = config_cache_span_entries(span_log2);
        uint32_t removed = 0;

        for (auto& entry : m_arch_config_cache) {
            if (entry.valid && entry.security_state == security_state &&
                config_cache_spans_overlap(entry.stream_base, entry.stream_span,
                                           base, span)) {
                entry = arch_config_cache_entry {};
                removed++;
            }
        }
        return removed;
    }

    void config_cache_fill(uint32_t stream_id, uint32_t security_state,
                           uint64_t ste0, uint64_t ste1, uint64_t ste2,
                           uint32_t span_log2 = 0)
    {
        const uint32_t base = config_cache_span_base(stream_id, span_log2);
        const uint32_t span = config_cache_span_entries(span_log2);
        auto* slot = static_cast<arch_config_cache_entry*>(nullptr);

        for (auto& entry : m_arch_config_cache) {
            if (entry.valid && entry.stream_base == base &&
                entry.stream_span == span && entry.security_state == security_state) {
                slot = &entry;
                break;
            }
            if (slot == nullptr && !entry.valid) {
                slot = &entry;
            }
        }
        if (slot == nullptr) {
            slot = &m_arch_config_cache[0];
        }

        slot->stream_base = base;
        slot->stream_span = span;
        slot->security_state = security_state;
        slot->ste0 = ste0;
        slot->ste1 = ste1;
        slot->ste2 = ste2;
        slot->valid = true;
    }

    bool config_cache_conflict_present(uint32_t stream_id, uint32_t security_state,
                                       uint64_t ste0, uint64_t ste1, uint64_t ste2,
                                       uint32_t span_log2 = 0) const
    {
        const uint32_t base = config_cache_span_base(stream_id, span_log2);
        const uint32_t span = config_cache_span_entries(span_log2);

        for (const auto& entry : m_arch_config_cache) {
            if (!entry.valid || entry.security_state != security_state) {
                continue;
            }
            if (config_cache_spans_overlap(entry.stream_base, entry.stream_span,
                                           base, span) &&
                !config_cache_ste_matches(entry, ste0, ste1, ste2)) {
                return true;
            }
        }
        return false;
    }

    bool record_config_cache_conflict_if_present(uint32_t stream_id, uint64_t iova,
                                                 uint32_t security_state,
                                                 uint64_t ste0, uint64_t ste1,
                                                 uint64_t ste2,
                                                 uint32_t span_log2 = 0)
    {
        if (!config_cache_conflict_present(stream_id, security_state, ste0, ste1,
                                           ste2, span_log2)) {
            return false;
        }

        m_arch_fault_reason = ARCH_FAULT_CFG_CONFLICT;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        record_fault(stream_id, "cfg-conflict", iova, PAGE_SIZE);
        m_arch_cfg_conflict_recoveries +=
            clear_config_cache_span(stream_id, security_state, span_log2);
        return true;
    }

    void ats_fill(uint32_t stream_id, uint64_t page)
    {
        ats_fill(stream_id, page, 0, 0, false, 0);
    }

    void ats_fill(uint32_t stream_id, uint64_t page, uint16_t asid, uint16_t vmid,
                  bool ssid_valid = false, uint32_t ssid = 0,
                  uint8_t security_state = ARCH_SECURITY_NONSECURE,
                  uint32_t leaf_level = ARCH_LEVELS - 1,
                  uint64_t granule_bytes = PAGE_SIZE,
                  uint8_t tlbi_regime = ARCH_TLBI_REGIME_NSNH)
    {
        security_state &= ARCH_SECURITY_EVENTQ_STATE_MASK;
        if (ats_lookup(stream_id, page, asid, vmid, ssid_valid, ssid,
                       security_state)) {
            return;
        }
        auto* slot = static_cast<ats_entry*>(nullptr);
        for (auto& entry : m_ats_cache) {
            if (!entry.valid) {
                slot = &entry;
                break;
            }
        }
        if (slot == nullptr) {
            slot = &m_ats_cache[m_ats_fills % m_ats_cache.size()];
        } else {
            m_ats_entries++;
        }
        slot->stream_id = stream_id;
        slot->page = page;
        slot->asid = asid;
        slot->vmid = vmid;
        slot->ssid = ssid;
        slot->leaf_level = leaf_level;
        slot->granule_bytes = granule_bytes;
        slot->security_state = security_state;
        slot->tlbi_regime = tlbi_regime;
        slot->ssid_valid = ssid_valid;
        slot->valid = true;
        m_ats_fills++;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS cache fill stream-id=0x" << std::hex << stream_id
                     << " iova-page=0x" << page << " asid=0x" << asid
                     << " vmid=0x" << vmid
                     << " security-state=0x"
                     << static_cast<uint32_t>(security_state);
        if (ssid_valid) {
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS cache fill ssid=0x" << std::hex << ssid;
        }
        SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS cache entries=" << std::dec << m_ats_entries;
        std::cerr << "APOLLO_SMMU_TBU: ATS cache fill stream-id=0x" << std::hex << stream_id
                  << " iova-page=0x" << page << " asid=0x" << asid << " vmid=0x"
                  << vmid << " security-state=0x"
                  << static_cast<uint32_t>(security_state)
                  << " leaf-level=" << std::dec << leaf_level
                  << " granule=0x" << std::hex << granule_bytes
                  << " tlbi-regime=0x" << static_cast<uint32_t>(tlbi_regime);
        if (ssid_valid) {
            std::cerr << " ssid=0x" << ssid;
        }
        std::cerr << " entries=" << std::dec << m_ats_entries << std::endl;
    }

    void record_page_walk(uint32_t stream_id, uint64_t iova, uint64_t pa, uint64_t len)
    {
        record_page_walk(stream_id, iova, pa, len, false, 0);
    }

    void record_page_walk(uint32_t stream_id, uint64_t iova, uint64_t pa, uint64_t len,
                          bool ssid_valid, uint32_t ssid)
    {
        const uint64_t page = page_base(iova);

        ats_fill(stream_id, page, 0, 0, ssid_valid, ssid,
                 m_arch_last_security_state);
        SCP_INFO(()) << "APOLLO_SMMU_TBU: page-table walk stream-id=0x" << std::hex << stream_id
                     << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len;
        if (ssid_valid) {
            SCP_INFO(()) << " APOLLO_SMMU_TBU: endpoint substream-id=0x" << std::hex << ssid;
        }
        SCP_INFO(()) << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: page-table walk stream-id=0x" << std::hex << stream_id
                  << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len;
        if (ssid_valid) {
            std::cerr << " endpoint-ssid=0x" << ssid;
        }
        std::cerr << std::dec << std::endl;
    }

    void log_pri_resolved(uint32_t stream_id, uint64_t iova, uint64_t pa, uint64_t len)
    {
        m_pri_requests += page_count(len);
        push_pri_record(stream_id, iova, pa, len);
        SCP_INFO(()) << "APOLLO_SMMU_TBU: PRI request resolved stream-id=0x" << std::hex << stream_id
                     << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                     << " pages=" << std::dec << page_count(len);
        std::cerr << "APOLLO_SMMU_TBU: PRI request resolved stream-id=0x" << std::hex << stream_id
                  << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                  << " pages=" << std::dec << page_count(len) << std::endl;
    }

    uint16_t record_fault(uint32_t stream_id, const char* op, uint64_t iova, uint64_t len,
                          bool stall = false, bool ssid_valid = false, uint32_t ssid = 0,
                          bool privileged = false, bool instruction = false)
    {
        const bool write = std::strcmp(op, "write") == 0;
        const bool event_privileged =
            privileged || (op != nullptr && std::strstr(op, "privileged") != nullptr);
        const bool event_instruction =
            !write &&
            (instruction ||
             (op != nullptr &&
              (std::strstr(op, "instruction") != nullptr ||
               std::strstr(op, "execute") != nullptr)));
        const bool event_ssid_valid = ssid_valid || m_arch_current_ssid_valid ||
                                      m_arch_selected_ssid_valid;
        const uint32_t event_ssid =
            ssid_valid ? ssid :
                         (m_arch_current_ssid_valid ? m_arch_current_ssid : m_arch_selected_ssid);
        uint16_t stag = 0;

        m_last_fault_iova = iova;
        m_fault_count++;
        if (stall) {
            const uint32_t suppressed_before = m_arch_stall_suppressed;
            stag = allocate_stall_record(stream_id, iova, event_ssid_valid, event_ssid);
            if (m_arch_stall_suppressed != suppressed_before) {
                return stag;
            }
        }
        push_event_record(stream_id, op, iova, len, stall, write, event_ssid_valid,
                          event_ssid, stag, event_privileged, event_instruction);
        SCP_WARN(()) << "APOLLO_SMMU_TBU: fault queue push stream-id=0x" << std::hex << stream_id
                     << " " << op << " iova=0x" << iova << " len=0x" << len << " detail=0x"
                     << m_arch_last_fault_detail;
        if (event_ssid_valid) {
            SCP_WARN(()) << " endpoint-ssid=0x" << std::hex << event_ssid;
        }
        SCP_WARN(()) << " count=" << std::dec << m_fault_count;
        std::cerr << "APOLLO_SMMU_TBU: fault queue push stream-id=0x" << std::hex << stream_id
                  << " " << op << " iova=0x" << iova << " len=0x" << len << " detail=0x"
                  << m_arch_last_fault_detail;
        if (event_ssid_valid) {
            std::cerr << " endpoint-ssid=0x" << event_ssid;
        }
        std::cerr << " count=" << std::dec << m_fault_count << std::endl;
        return stag;
    }

    void clear_faults()
    {
        m_fault_count = 0;
        m_last_fault_iova = 0;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        m_arch_fault_nsipa = false;
        m_arch_fault_gpcf = false;
        m_arch_fault_record_suppressed = false;
        m_arch_fault_stage2_stall = false;
        reset_arch_mpam_state();
        m_arch_fault_record_suppressed = false;
        m_arch_last_fault_detail = 0;
        m_arch_last_fetch_addr = 0;
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_e0pd_fault = false;
        m_arch_last_had0 = false;
        m_arch_last_had1 = false;
        m_arch_last_had_disabled_hier_attrs = false;
        m_arch_last_hier_attrs_applied = false;
        m_arch_last_xnx_fault = false;
        m_arch_last_bbml2_nt_ignored = false;
        m_arch_last_bbml2_nt_desc = 0;
        m_arch_last_httu_af_update = false;
        m_arch_last_httu_dirty_update = false;
        m_arch_last_httu_desc_pa = 0;
        m_arch_last_httu_desc_before = 0;
        m_arch_last_httu_desc_after = 0;
        m_arch_last_ats_treq_write = false;
        m_arch_last_ptwnnc_device_fetch = false;
        m_arch_last_ptwnnc_normalized = false;
        m_arch_last_s2ptw_fault = false;
        clear_stall_records();
        SCP_INFO(()) << "APOLLO_SMMU_TBU: fault queue cleared stream-id=0x" << std::hex
                     << p_stream_id.get_value() << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: fault queue cleared stream-id=0x" << std::hex
                  << p_stream_id.get_value() << std::dec << std::endl;
    }

    void inject_fault()
    {
        record_fault(selected_map_stream_id(), "probe", m_map_iova, m_map_size ? m_map_size : PAGE_SIZE);
        m_map_status = MAP_STATUS_OK;
    }

    bool read_downstream_u64(uint64_t pa, uint64_t& value, bool apply_gmpam = false,
                             bool apply_current_mpam = false)
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

        gs::ApolloSmmuStreamIdExtension mpam_ext(p_stream_id.get_value());
        bool attached_mpam = false;
        if (apply_gmpam) {
            populate_arch_mpam_extension(
                mpam_ext, arch_gmpam_for_security_state(m_arch_last_security_state));
            trans.set_extension(&mpam_ext);
            attached_mpam = true;
        } else if (apply_current_mpam && m_arch_last_mpam_valid) {
            populate_arch_mpam_extension_from_state(mpam_ext);
            trans.set_extension(&mpam_ext);
            attached_mpam = true;
        }
        downstream->b_transport(trans, delay);
        if (attached_mpam) {
            trans.clear_extension(&mpam_ext);
        }
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

    bool execute_descriptor_memory_read(const arch_descriptor_memory_read& read,
                                        uint64_t& desc)
    {
        return m_arch_io_executor->read_descriptor(*this, read, desc);
    }

    tlm::tlm_response_status execute_endpoint_replay_transaction(
        const arch_endpoint_replay_transaction& transaction,
        sc_core::sc_time& delay)
    {
        return m_arch_io_executor->replay_transaction(*this, transaction, delay);
    }

    void set_arch_io_executor_for_testing(arch_io_executor* executor)
    {
        m_arch_io_executor =
            executor == nullptr ? &m_default_arch_io_executor : executor;
    }

    const arch_io_executor* arch_io_executor_for_testing() const
    {
        return m_arch_io_executor;
    }

    tlm::tlm_response_status execute_endpoint_replay_transaction_tlm(
        const arch_endpoint_replay_transaction& transaction,
        sc_core::sc_time& delay)
    {
        if (!transaction.valid || transaction.payload == nullptr ||
            transaction.len >
                static_cast<uint64_t>(std::numeric_limits<unsigned int>::max())) {
            return tlm::TLM_BURST_ERROR_RESPONSE;
        }

        tlm::tlm_generic_payload trans;
        trans.set_command(transaction.write ? tlm::TLM_WRITE_COMMAND :
                                              tlm::TLM_READ_COMMAND);
        trans.set_address(transaction.pa);
        trans.set_data_ptr(transaction.payload);
        trans.set_data_length(static_cast<unsigned int>(transaction.len));
        trans.set_streaming_width(static_cast<unsigned int>(transaction.len));
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        downstream->b_transport(trans, delay);
        return trans.get_response_status();
    }

    bool write_downstream_u64(uint64_t pa, uint64_t value)
    {
        std::array<uint8_t, sizeof(value)> data {};
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        std::memcpy(data.data(), &value, sizeof(value));
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(pa);
        trans.set_data_ptr(data.data());
        trans.set_data_length(data.size());
        trans.set_streaming_width(data.size());
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        gs::ApolloSmmuStreamIdExtension mpam_ext(p_stream_id.get_value());
        populate_arch_mpam_extension(
            mpam_ext, arch_gmpam_for_security_state(m_arch_last_security_state));
        trans.set_extension(&mpam_ext);
        downstream->b_transport(trans, delay);
        trans.clear_extension(&mpam_ext);
        if (trans.is_response_ok()) {
            return true;
        }

        SCP_WARN(()) << "APOLLO_SMMU_TBU: queue record write failed stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " pa=0x" << pa << " response="
                     << trans.get_response_string() << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: queue record write failed stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " pa=0x" << pa << " response="
                  << trans.get_response_string() << std::dec << std::endl;
        return false;
    }

    bool write_downstream_u32(uint64_t pa, uint32_t value)
    {
        std::array<uint8_t, sizeof(value)> data {};
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        std::memcpy(data.data(), &value, sizeof(value));
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(pa);
        trans.set_data_ptr(data.data());
        trans.set_data_length(data.size());
        trans.set_streaming_width(data.size());
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        gs::ApolloSmmuStreamIdExtension mpam_ext(p_stream_id.get_value());
        populate_arch_mpam_extension(
            mpam_ext, arch_gmpam_for_security_state(m_arch_last_security_state));
        trans.set_extension(&mpam_ext);
        downstream->b_transport(trans, delay);
        trans.clear_extension(&mpam_ext);
        if (trans.is_response_ok()) {
            return true;
        }

        SCP_WARN(()) << "APOLLO_SMMU_TBU: MSI write failed stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " pa=0x" << pa << " data=0x" << value
                     << " response=" << trans.get_response_string() << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: MSI write failed stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " pa=0x" << pa << " data=0x" << value
                  << " response=" << trans.get_response_string() << std::dec << std::endl;
        return false;
    }

    bool is_smmuv3_reg(uint64_t addr) const
    {
        return m_arch_core.classify_register(addr) ==
               apollo::smmuv3::apollo_smmu_arch_core::register_aperture::smmuv3;
    }

    static uint32_t queue_entries(const arch_queue& queue)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::queue_entries(queue.base);
    }

    static uint64_t queue_base_addr(const arch_queue& queue)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::queue_base_addr(queue.base);
    }

    static uint32_t queue_index(uint32_t value, uint32_t entries)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::queue_index(value, entries);
    }

    static uint32_t queue_advance(uint32_t value, uint32_t entries)
    {
        if (entries == 0) {
            return value;
        }

        const uint32_t mask = entries - 1;
        const uint32_t wrap = value & entries;
        uint32_t index = (value & mask) + 1;
        uint32_t next_wrap = wrap;

        if (index == entries) {
            index = 0;
            next_wrap ^= entries;
        }
        return next_wrap | index;
    }

    static uint32_t queue_prod_reg(const arch_queue& queue)
    {
        return (queue.prod & ARCH_QUEUE_INDEX_MASK) |
               (queue.ovflg ? ARCH_QUEUE_OVFLG : 0);
    }

    static uint32_t queue_cons_reg(const arch_queue& queue)
    {
        return (queue.cons & ARCH_QUEUE_INDEX_MASK) |
               (queue.ovackflg ? ARCH_QUEUE_OVFLG : 0);
    }

    static bool queue_overflow_active(const arch_queue& queue)
    {
        return queue.ovflg != queue.ovackflg;
    }

    static bool arch_queue_would_overflow(const arch_queue& queue)
    {
        const uint32_t entries = queue_entries(queue);

        if (entries == 0) {
            return true;
        }

        const uint32_t next_prod = queue_advance(queue.prod, entries);

        return queue_index(next_prod, entries) == queue_index(queue.cons, entries) &&
               ((next_prod ^ queue.cons) & entries) != 0;
    }

    static bool arch_queue_writable(const arch_queue& queue)
    {
        return queue_entries(queue) != 0 && queue_base_addr(queue) != 0 &&
               !arch_queue_would_overflow(queue);
    }

    static uint64_t arch_queue_next_record_addr(const arch_queue& queue,
                                                uint32_t entry_bytes)
    {
        const uint32_t entries = queue_entries(queue);
        const uint64_t base = queue_base_addr(queue);

        if (entries == 0 || base == 0) {
            return 0;
        }
        return base + queue_index(queue.prod, entries) * entry_bytes;
    }

    void set_arch_queue_overflow(arch_queue& queue, const char* queue_name,
                                 uint8_t security_state = ARCH_SECURITY_NONSECURE)
    {
        queue.overflow = true;
        if (!queue_overflow_active(queue)) {
            queue.ovflg = !queue.ovflg;
        }
        if (arch_security_uses_secure_irq_bank(security_state)) {
            set_secure_gerror(ARCH_GERROR_QUEUE_OVERFLOW);
        } else {
            set_arch_gerror(ARCH_GERROR_QUEUE_OVERFLOW);
        }
        log_arch_queue(queue_name, "overflow", queue_prod_reg(queue), queue_cons_reg(queue));
    }

    void write_arch_output_queue_cons(arch_queue& queue, uint32_t value)
    {
        queue.cons = value & ARCH_QUEUE_INDEX_MASK;
        queue.ovackflg = (value & ARCH_QUEUE_OVFLG) != 0;
        if (!queue_overflow_active(queue)) {
            queue.overflow = false;
        }
    }

    bool buffer_stall_event_record(const std::array<uint64_t, 4>& words,
                                   uint8_t security_state)
    {
        m_arch_stall_event_buffer.push_back(arch_stall_event_buffer_record {
            words,
            static_cast<uint8_t>(security_state & ARCH_SECURITY_EVENTQ_STATE_MASK),
        });
        m_arch_stall_buffered = static_cast<uint32_t>(m_arch_stall_event_buffer.size());
        log_arch_queue("EVENTQ", "stall-buffer", queue_prod_reg(m_eventq), queue_cons_reg(m_eventq));
        return true;
    }

    void drain_stall_event_buffer()
    {
        while (!m_arch_stall_event_buffer.empty()) {
            const auto buffered = m_arch_stall_event_buffer.front();
            arch_queue& target_eventq =
                arch_eventq_for_security_state(buffered.security_state);

            if (!arch_queue_writable(target_eventq)) {
                break;
            }

            const uint64_t guest_record_addr =
                arch_queue_next_record_addr(target_eventq, ARCH_EVENTQ_ENTRY_BYTES);

            if (!push_arch_queue_record(target_eventq, "EVENTQ", ARCH_EVENTQ_ENTRY_BYTES,
                                        buffered.words, buffered.words.size(),
                                        ARCH_GERROR_EVENTQ_ABORT,
                                        buffered.security_state)) {
                break;
            }
            record_arch_eventq_security_route(buffered.security_state, buffered.words,
                                             guest_record_addr, queue_prod_reg(target_eventq),
                                             queue_cons_reg(target_eventq));
            m_arch_stall_event_buffer.pop_front();
            m_arch_stall_buffered = static_cast<uint32_t>(m_arch_stall_event_buffer.size());
            m_arch_stall_redriven++;
            set_arch_irq_status_for_security_state(ARCH_IRQ_EVENTQ,
                                                   buffered.security_state);
            log_arch_queue("EVENTQ", "stall-redrive", queue_prod_reg(target_eventq),
                           queue_cons_reg(target_eventq));
        }
    }

    void log_arch_queue(const char* queue_name, const char* action, uint32_t prod, uint32_t cons)
    {
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected " << queue_name << " " << action
                     << " stream-id=0x" << std::hex << p_stream_id.get_value() << " prod=0x" << prod
                     << " cons=0x" << cons << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected " << queue_name << " " << action << " stream-id=0x"
                  << std::hex << p_stream_id.get_value() << " prod=0x" << prod << " cons=0x" << cons
                  << std::dec << std::endl;
    }

    uint32_t read_cmdq_cons() const
    {
        return (m_cmdq.cons & ARCH_CMDQ_CONS_RD_MASK) |
               ((m_arch_cmdq_cerror & ARCH_CMDQ_CONS_ERR_MASK) << ARCH_CMDQ_CONS_ERR_SHIFT);
    }

    void write_cmdq_cons(uint32_t value)
    {
        m_cmdq.cons = value & ARCH_CMDQ_CONS_RD_MASK;
        m_arch_cmdq_cerror = (value >> ARCH_CMDQ_CONS_ERR_SHIFT) & ARCH_CMDQ_CONS_ERR_MASK;
    }

    void set_cmdq_cerror(uint32_t cerror, const char* reason)
    {
        m_arch_cmdq_cerror = cerror & ARCH_CMDQ_CONS_ERR_MASK;
        set_arch_gerror(ARCH_GERROR_CMDQ_ABORT);
        SCP_WARN(()) << "APOLLO_SMMU_TBU: architected CMDQ CERROR"
                     << " stream-id=0x" << std::hex << p_stream_id.get_value()
                     << " cerror=0x" << m_arch_cmdq_cerror
                     << " rd=0x" << (m_cmdq.cons & ARCH_CMDQ_CONS_RD_MASK)
                     << " reason=" << reason << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected CMDQ CERROR"
                  << " stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " cerror=0x" << m_arch_cmdq_cerror
                  << " rd=0x" << (m_cmdq.cons & ARCH_CMDQ_CONS_RD_MASK)
                  << " reason=" << reason << std::dec << std::endl;
    }

    uint32_t read_secure_cmdq_cons() const
    {
        return (m_arch_secure_cmdq.cons & ARCH_CMDQ_CONS_RD_MASK) |
               ((m_arch_secure_cmdq_cerror & ARCH_CMDQ_CONS_ERR_MASK)
                << ARCH_CMDQ_CONS_ERR_SHIFT);
    }

    void write_secure_cmdq_cons(uint32_t value)
    {
        m_arch_secure_cmdq.cons = value & ARCH_CMDQ_CONS_RD_MASK;
        m_arch_secure_cmdq_cerror =
            (value >> ARCH_CMDQ_CONS_ERR_SHIFT) & ARCH_CMDQ_CONS_ERR_MASK;
    }

    void set_secure_cmdq_cerror(uint32_t cerror, const char* reason)
    {
        m_arch_secure_cmdq_cerror = cerror & ARCH_CMDQ_CONS_ERR_MASK;
        set_secure_gerror(ARCH_GERROR_CMDQ_ABORT);
        SCP_WARN(()) << "APOLLO_SMMU_TBU: architected S_CMDQ CERROR"
                     << " stream-id=0x" << std::hex << p_stream_id.get_value()
                     << " cerror=0x" << m_arch_secure_cmdq_cerror
                     << " rd=0x" << (m_arch_secure_cmdq.cons & ARCH_CMDQ_CONS_RD_MASK)
                     << " reason=" << reason << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected S_CMDQ CERROR"
                  << " stream-id=0x" << std::hex << p_stream_id.get_value()
                  << " cerror=0x" << m_arch_secure_cmdq_cerror
                  << " rd=0x" << (m_arch_secure_cmdq.cons & ARCH_CMDQ_CONS_RD_MASK)
                  << " reason=" << reason << std::dec << std::endl;
    }

    uint32_t read_arch_gerror_raw() const
    {
        return m_arch_gerror & ARCH_GERROR_KNOWN_MASK;
    }

    uint32_t read_arch_gerror() const
    {
        return (m_arch_gerror ^ m_arch_gerrorn) & ARCH_GERROR_KNOWN_MASK;
    }

    uint32_t read_secure_gerror_raw() const
    {
        return m_arch_secure_gerror & ARCH_GERROR_KNOWN_MASK;
    }

    uint32_t read_secure_gerror() const
    {
        return (m_arch_secure_gerror ^ m_arch_secure_gerrorn) & ARCH_GERROR_KNOWN_MASK;
    }

    bool any_arch_gerror_active() const
    {
        return read_arch_gerror() != 0 || read_secure_gerror() != 0;
    }

    void set_arch_gerror(uint32_t mask)
    {
        const uint32_t known_mask = mask & ARCH_GERROR_KNOWN_MASK;
        const uint32_t inactive = known_mask & ~read_arch_gerror();

        m_arch_gerror ^= inactive;
        if ((inactive & ~ARCH_GERROR_MSI_GERROR_ABORT) != 0) {
            set_arch_irq_status_for_security_state(ARCH_IRQ_GERROR,
                                                   ARCH_SECURITY_NONSECURE);
        }
    }

    void ack_arch_gerror(uint32_t value)
    {
        m_arch_gerrorn ^= value & read_arch_gerror();
        if (!any_arch_gerror_active()) {
            clear_arch_irq_status(ARCH_IRQ_GERROR);
        }
    }

    void set_secure_gerror(uint32_t mask)
    {
        const uint32_t known_mask = mask & ARCH_GERROR_KNOWN_MASK;
        const uint32_t inactive = known_mask & ~read_secure_gerror();

        m_arch_secure_gerror ^= inactive;
        if ((inactive & ~ARCH_GERROR_MSI_GERROR_ABORT) != 0) {
            set_arch_irq_status_for_security_state(ARCH_IRQ_GERROR,
                                                   ARCH_SECURITY_SECURE);
        }
    }

    void ack_secure_gerror(uint32_t value)
    {
        m_arch_secure_gerrorn ^= value & read_secure_gerror();
        if (!any_arch_gerror_active()) {
            clear_arch_irq_status(ARCH_IRQ_GERROR);
        }
    }

    bool arch_irq_cfg_writable(uint32_t irq_mask) const
    {
        return ((m_arch_irq_ctrl | m_arch_irq_ctrlack) & irq_mask) == 0;
    }

    bool arch_secure_irq_cfg_writable(uint32_t irq_mask) const
    {
        return ((m_arch_secure_irq_ctrl | m_arch_secure_irq_ctrlack) & irq_mask) == 0;
    }

    void normalize_msi_cfg(arch_msi_config& cfg)
    {
        cfg.addr &= ARCH_MSI_CFG0_ADDR_MASK;
        cfg.attr &= ARCH_MSI_CFG2_WRITABLE_MASK;
    }

    bool arch_msi_addr_nonzero(uint64_t addr) const
    {
        return (addr & ARCH_MSI_CFG0_ADDR_MASK) != 0;
    }

    void record_arch_msi(uint32_t source, uint64_t addr, uint32_t data)
    {
        m_arch_msi_writes++;
        m_arch_last_msi_source = source;
        m_arch_last_msi_addr = addr & ARCH_MSI_CFG0_ADDR_MASK;
        m_arch_last_msi_data = data;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected MSI write stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " source=0x" << source
                     << " addr=0x" << m_arch_last_msi_addr << " data=0x" << data
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected MSI write stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " source=0x" << source
                  << " addr=0x" << m_arch_last_msi_addr << " data=0x" << data
                  << std::dec << std::endl;
    }

    void emit_arch_msi(uint32_t source, uint64_t addr, uint32_t data, uint32_t abort_gerror)
    {
        addr &= ARCH_MSI_CFG0_ADDR_MASK;
        if (!arch_msi_addr_nonzero(addr)) {
            return;
        }

        if (write_downstream_u32(addr, data)) {
            record_arch_msi(source, addr, data);
            return;
        }

        m_arch_msi_aborts++;
        set_arch_gerror(abort_gerror);
    }

    void emit_secure_arch_msi(uint32_t source, uint64_t addr, uint32_t data,
                              uint32_t abort_gerror)
    {
        addr &= ARCH_MSI_CFG0_ADDR_MASK;
        if (!arch_msi_addr_nonzero(addr)) {
            return;
        }

        if (write_downstream_u32(addr, data)) {
            record_arch_msi(source, addr, data);
            return;
        }

        m_arch_msi_aborts++;
        set_secure_gerror(abort_gerror);
    }

    void emit_secure_cmdq_sync_msi(uint64_t addr, uint32_t data)
    {
        emit_secure_arch_msi(ARCH_IRQ_CMDQ_SYNC, addr, data,
                             ARCH_GERROR_MSI_CMDQ_ABORT);
    }

    void emit_arch_irq_msi(uint32_t source)
    {
        switch (source) {
        case ARCH_IRQ_EVENTQ:
            if (arch_security_uses_secure_irq_bank(m_arch_eventq_irq_security_state)) {
                emit_secure_arch_msi(source, m_arch_secure_eventq_msi.addr,
                                     m_arch_secure_eventq_msi.data,
                                     ARCH_GERROR_MSI_EVENTQ_ABORT);
            } else {
                emit_arch_msi(source, m_arch_eventq_msi.addr, m_arch_eventq_msi.data,
                              ARCH_GERROR_MSI_EVENTQ_ABORT);
            }
            break;
        case ARCH_IRQ_PRIQ:
            if (arch_security_uses_secure_irq_bank(m_arch_priq_irq_security_state)) {
                emit_secure_arch_msi(source, m_arch_secure_priq_msi.addr,
                                     m_arch_secure_priq_msi.data,
                                     ARCH_GERROR_MSI_PRIQ_ABORT);
            } else {
                emit_arch_msi(source, m_arch_priq_msi.addr, m_arch_priq_msi.data,
                              ARCH_GERROR_MSI_PRIQ_ABORT);
            }
            break;
        case ARCH_IRQ_GERROR:
            if (arch_security_uses_secure_irq_bank(m_arch_gerror_irq_security_state)) {
                emit_secure_arch_msi(source, m_arch_secure_gerror_msi.addr,
                                     m_arch_secure_gerror_msi.data,
                                     ARCH_GERROR_MSI_GERROR_ABORT);
            } else {
                emit_arch_msi(source, m_arch_gerror_msi.addr, m_arch_gerror_msi.data,
                              ARCH_GERROR_MSI_GERROR_ABORT);
            }
            break;
        default:
            break;
        }
    }

    void set_arch_irq_status(uint32_t mask)
    {
        set_arch_irq_status_for_security_state(mask, ARCH_SECURITY_NONSECURE);
    }

    void set_arch_irq_status_for_security_state(uint32_t mask, uint8_t security_state)
    {
        m_arch_irq_status |= mask;
        if ((mask & ARCH_IRQ_EVENTQ) != 0) {
            m_arch_eventq_irq_security_state =
                arch_security_uses_secure_irq_bank(security_state) ?
                    ARCH_SECURITY_SECURE :
                    ARCH_SECURITY_NONSECURE;
        }
        if ((mask & ARCH_IRQ_PRIQ) != 0) {
            m_arch_priq_irq_security_state =
                arch_security_uses_secure_irq_bank(security_state) ?
                    ARCH_SECURITY_SECURE :
                    ARCH_SECURITY_NONSECURE;
        }
        if ((mask & ARCH_IRQ_CMDQ_SYNC) != 0) {
            m_arch_cmdq_sync_irq_security_state =
                arch_security_uses_secure_irq_bank(security_state) ?
                    ARCH_SECURITY_SECURE :
                    ARCH_SECURITY_NONSECURE;
        }
        if ((mask & ARCH_IRQ_GERROR) != 0) {
            m_arch_gerror_irq_security_state =
                arch_security_uses_secure_irq_bank(security_state) ?
                    ARCH_SECURITY_SECURE :
                    ARCH_SECURITY_NONSECURE;
        }
        update_irq_outputs();
    }

    void clear_arch_irq_status(uint32_t mask)
    {
        m_arch_irq_status &= ~mask;
        update_irq_outputs();
    }

    void update_irq_outputs()
    {
        uint32_t active = 0;
        if ((m_arch_irq_status & ARCH_IRQ_EVENTQ) != 0) {
            const uint32_t ctrl =
                arch_security_uses_secure_irq_bank(m_arch_eventq_irq_security_state) ?
                    m_arch_secure_irq_ctrl :
                    m_arch_irq_ctrl;
            active |= ctrl & ARCH_IRQ_EVENTQ;
        }
        if ((m_arch_irq_status & ARCH_IRQ_PRIQ) != 0) {
            const uint32_t ctrl =
                arch_security_uses_secure_irq_bank(m_arch_priq_irq_security_state) ?
                    m_arch_secure_irq_ctrl :
                    m_arch_irq_ctrl;
            active |= ctrl & ARCH_IRQ_PRIQ;
        }
        if ((m_arch_irq_status & ARCH_IRQ_CMDQ_SYNC) != 0) {
            const uint32_t ctrl =
                arch_security_uses_secure_irq_bank(
                    m_arch_cmdq_sync_irq_security_state) ?
                    m_arch_secure_irq_ctrl :
                    m_arch_irq_ctrl;
            active |= ctrl & ARCH_IRQ_CMDQ_SYNC;
        }
        if ((m_arch_irq_status & ARCH_IRQ_GERROR) != 0) {
            const uint32_t ctrl =
                arch_security_uses_secure_irq_bank(m_arch_gerror_irq_security_state) ?
                    m_arch_secure_irq_ctrl :
                    m_arch_irq_ctrl;
            active |= ctrl & ARCH_IRQ_GERROR;
        }
        const uint32_t rising = active & ~m_arch_irq_lines;

        if (active == m_arch_irq_lines) {
            return;
        }

        m_arch_irq_lines = active;
        for (size_t i = 0; i < irq_out.size(); i++) {
            const bool asserted = (active & (1u << i)) != 0;
            if (irq_out[i].size() > 0) {
                irq_out[i]->write(asserted);
            }
        }

        emit_arch_irq_msi(rising & ARCH_IRQ_EVENTQ);
        emit_arch_irq_msi(rising & ARCH_IRQ_PRIQ);
        emit_arch_irq_msi(rising & ARCH_IRQ_GERROR);

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected IRQ update stream-id=0x" << std::hex
                     << p_stream_id.get_value() << " status=0x" << m_arch_irq_status << " ctrl=0x"
                     << m_arch_irq_ctrl << " lines=0x" << m_arch_irq_lines << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected IRQ update stream-id=0x" << std::hex
                  << p_stream_id.get_value() << " status=0x" << m_arch_irq_status << " ctrl=0x"
                  << m_arch_irq_ctrl << " lines=0x" << m_arch_irq_lines << std::dec << std::endl;
    }

    bool arch_cr0_enabled(uint32_t mask) const
    {
        return (m_arch_cr0ack & mask) == mask;
    }

    bool arch_cmdq_enabled() const
    {
        return arch_cr0_enabled(ARCH_CR0_SMMUEN | ARCH_CR0_CMDQEN);
    }

    bool arch_secure_cmdq_enabled() const
    {
        return (m_arch_secure_cr0ack & (ARCH_CR0_SMMUEN | ARCH_CR0_CMDQEN)) ==
               (ARCH_CR0_SMMUEN | ARCH_CR0_CMDQEN);
    }

    bool arch_eventq_enabled(bool stall, bool smmu_disabled_recordable = false) const
    {
        /*
         * EVENTQEN gates normal event recording.  Stall events still need a
         * memory-visible record so software can read the STAG and issue
         * CMD_RESUME/CMD_STALL_TERM.
         */
        if (stall) {
            return true;
        }
        if (smmu_disabled_recordable) {
            return arch_cr0_enabled(ARCH_CR0_EVENTQEN);
        }
        return arch_cr0_enabled(ARCH_CR0_SMMUEN | ARCH_CR0_EVENTQEN);
    }

    bool arch_eventq_requested() const
    {
        return (m_arch_cr0ack & ARCH_CR0_EVENTQEN) != 0;
    }

    bool arch_priq_enabled() const
    {
        return arch_cr0_enabled(ARCH_CR0_SMMUEN | ARCH_CR0_PRIQEN);
    }

    bool arch_secure_priq_enabled() const
    {
        return (m_arch_secure_cr0ack & (ARCH_CR0_SMMUEN | ARCH_CR0_PRIQEN)) ==
               (ARCH_CR0_SMMUEN | ARCH_CR0_PRIQEN);
    }

    bool arch_priq_enabled_for_security_state(uint8_t security_state) const
    {
        if (arch_security_uses_secure_irq_bank(security_state)) {
            return arch_secure_priq_enabled();
        }

        return arch_priq_enabled();
    }

    bool arch_smmu_enabled() const
    {
        return arch_cr0_enabled(ARCH_CR0_SMMUEN);
    }

    bool arch_smmu_enabled_for_security_state(uint8_t security_state) const
    {
        if (arch_security_uses_secure_irq_bank(security_state)) {
            return (m_arch_secure_cr0ack & ARCH_CR0_SMMUEN) != 0;
        }

        return arch_smmu_enabled();
    }

    bool arch_atschk_enabled() const
    {
        return arch_cr0_enabled(ARCH_CR0_SMMUEN | ARCH_CR0_ATSCHK);
    }

    static bool arch_ats_supported()
    {
        return (ARCH_IDR0 & ARCH_IDR0_ATS) != 0;
    }

    static bool arch_pri_supported()
    {
        return (ARCH_IDR0 & ARCH_IDR0_PRI) != 0;
    }

    bool arch_rec_cfg_ats_enabled() const
    {
        return (m_arch_cr2 & ARCH_CR2_REC_CFG_ATS) != 0;
    }

    bool arch_recinvsid_enabled() const
    {
        return (m_arch_cr2 & ARCH_CR2_RECINVSID) != 0;
    }

    bool arch_record_smmuen_disabled_ats_treq() const
    {
        return arch_eventq_requested() && arch_rec_cfg_ats_enabled();
    }

    bool arch_record_bad_streamid_ats_treq() const
    {
        return arch_eventq_requested() && arch_rec_cfg_ats_enabled() &&
               arch_recinvsid_enabled();
    }

    bool arch_record_bad_streamid_event() const
    {
        return arch_recinvsid_enabled();
    }

    bool arch_record_translated_config_fault() const
    {
        return arch_rec_cfg_ats_enabled();
    }

    bool push_arch_queue_record(arch_queue& queue, const char* queue_name, uint32_t entry_bytes,
                                const std::array<uint64_t, 4>& words, uint32_t word_count,
                                uint32_t abort_gerror,
                                uint8_t security_state = ARCH_SECURITY_NONSECURE)
    {
        const uint32_t entries = queue_entries(queue);
        const uint64_t base = queue_base_addr(queue);

        if (entries == 0 || base == 0) {
            return false;
        }

        const uint32_t next_prod = queue_advance(queue.prod, entries);
        if (arch_queue_would_overflow(queue)) {
            set_arch_queue_overflow(queue, queue_name, security_state);
            return false;
        }

        const uint64_t record_base = base + queue_index(queue.prod, entries) * entry_bytes;
        const uint32_t count = std::min<uint32_t>(word_count, words.size());

        for (uint32_t i = 0; i < count; i++) {
            if (!write_downstream_u64(record_base + i * sizeof(words[i]), words[i])) {
                if (arch_security_uses_secure_irq_bank(security_state)) {
                    set_secure_gerror(abort_gerror);
                } else {
                    set_arch_gerror(abort_gerror);
                }
                log_arch_queue(queue_name, "abort", queue_prod_reg(queue), queue_cons_reg(queue));
                return false;
            }
        }

        queue.prod = next_prod;
        log_arch_queue(queue_name, "push", queue.prod, queue.cons);
        return true;
    }

    static uint32_t arch_fault_class(uint32_t reason)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::fault_class(reason);
    }

    uint32_t arch_fault_detail_word(bool stall, bool write) const
    {
        return apollo::smmuv3::apollo_smmu_arch_core::fault_detail_word(
            m_arch_fault_reason, m_arch_fault_stage, m_arch_next_fault_replay_id, stall,
            write);
    }

    void set_arch_fetch_fault(uint32_t reason, uint32_t stage, uint64_t fetch_addr,
                              bool gpcf = false)
    {
        m_arch_core.set_fetch_fault(reason, stage, fetch_addr, gpcf);
    }

    bool stag_pending(uint16_t stag) const
    {
        return m_arch_core.stag_pending(stag);
    }

    arch_stall_record* find_stall(uint32_t stream_id, uint16_t stag)
    {
        return m_arch_core.find_stall(stream_id, stag);
    }

    void mark_stall_event_committed(uint32_t stream_id, uint16_t stag)
    {
        if (auto* const stall = find_stall(stream_id, stag)) {
            stall->event_committed = true;
        }
    }

    bool discard_buffered_stall_event(uint32_t stream_id, uint16_t stag)
    {
        for (auto it = m_arch_stall_event_buffer.begin();
             it != m_arch_stall_event_buffer.end(); ++it) {
            const uint32_t event_stream_id =
                static_cast<uint32_t>(it->words[0] >> 32);
            const uint16_t event_stag =
                static_cast<uint16_t>(it->words[1] & ARCH_EVENT_STAG_MASK);

            if (event_stream_id != stream_id || event_stag != stag) {
                continue;
            }

            m_arch_stall_event_buffer.erase(it);
            m_arch_stall_buffered =
                static_cast<uint32_t>(m_arch_stall_event_buffer.size());
            return true;
        }

        return false;
    }

    arch_stall_record* find_stall_by_fault(uint32_t stream_id, uint64_t iova,
                                           bool ssid_valid, uint32_t ssid)
    {
        return m_arch_core.find_stall_by_fault(stream_id, iova, ssid_valid, ssid);
    }

    arch_endpoint_replay_record* find_endpoint_replay(uint32_t stream_id, uint16_t stag)
    {
        return m_arch_core.find_pending_endpoint_replay(stream_id, stag);
    }

    arch_endpoint_replay_record* find_endpoint_replay_any(uint32_t stream_id, uint16_t stag)
    {
        return m_arch_core.find_endpoint_replay(stream_id, stag);
    }

    bool allocate_endpoint_replay_record(uint32_t stream_id, uint16_t stag, uint64_t iova,
                                         uint64_t len, bool write, bool ssid_valid,
                                         uint32_t ssid, const uint8_t* payload = nullptr,
                                         size_t payload_len = 0)
    {
        auto allocation = m_arch_core.allocate_endpoint_replay_record(
            stream_id, stag, iova, len, write, ssid_valid, ssid, payload, payload_len);

        if (allocation.invalid_stag) {
            return false;
        }
        if (allocation.duplicate) {
            return true;
        }
        if (allocation.allocated && allocation.record != nullptr) {
            SCP_WARN(()) << "APOLLO_SMMU_TBU: endpoint transaction stalled"
                         << " stream-id=0x" << std::hex << stream_id
                         << " stag=0x" << stag << " iova=0x" << iova
                         << " len=0x" << len << " write=" << write
                         << " payload=0x" << allocation.record->payload.size() << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: endpoint transaction stalled"
                      << " stream-id=0x" << std::hex << stream_id
                      << " stag=0x" << stag << " iova=0x" << iova
                      << " len=0x" << len << " write=" << write
                      << " payload=0x" << allocation.record->payload.size()
                      << std::dec << std::endl;
            return true;
        }

        SCP_WARN(()) << "APOLLO_SMMU_TBU: no free endpoint replay record"
                     << " stream-id=0x" << std::hex << stream_id
                     << " stag=0x" << stag << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: no free endpoint replay record"
                  << " stream-id=0x" << std::hex << stream_id
                  << " stag=0x" << stag << std::dec << std::endl;
        return false;
    }

    uint16_t allocate_stall_record(uint32_t stream_id, uint64_t iova, bool ssid_valid,
                                   uint32_t ssid)
    {
        arch_stall_record* free_slot = nullptr;
        if (auto* const existing = find_stall_by_fault(stream_id, iova, ssid_valid, ssid)) {
            m_arch_last_stag = existing->stag;
            m_arch_stall_suppressed++;
            m_arch_stall_merged++;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: stalled fault merged"
                         << " stream-id=0x" << std::hex << stream_id
                         << " stag=0x" << existing->stag
                         << " iova=0x" << iova << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: stalled fault merged"
                      << " stream-id=0x" << std::hex << stream_id
                      << " stag=0x" << existing->stag
                      << " iova=0x" << iova << std::dec << std::endl;
            return existing->stag;
        }
        for (auto& stall : m_arch_stalls) {
            if (!stall.pending) {
                free_slot = &stall;
                break;
            }
        }
        if (free_slot == nullptr) {
            m_arch_resume_unknown++;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: no free stall record"
                         << " stream-id=0x" << std::hex << stream_id << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: no free stall record"
                      << " stream-id=0x" << std::hex << stream_id << std::dec << std::endl;
            return 0;
        }

        for (uint32_t attempts = 0; attempts < ARCH_CMD_RESUME_STAG_MASK; ++attempts) {
            uint16_t stag = m_arch_next_stag++;
            if (m_arch_next_stag == 0) {
                m_arch_next_stag = 1;
            }
            if (stag == 0 || stag_pending(stag)) {
                continue;
            }

            *free_slot = arch_stall_record {
                stream_id,
                stag,
                iova,
                ssid,
                ssid_valid,
                true,
            };
            m_arch_stall_pending++;
            m_arch_last_stag = stag;
            return stag;
        }

        m_arch_resume_unknown++;
        SCP_WARN(()) << "APOLLO_SMMU_TBU: no reusable STAG available"
                     << " stream-id=0x" << std::hex << stream_id << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: no reusable STAG available"
                  << " stream-id=0x" << std::hex << stream_id << std::dec << std::endl;
        return 0;
    }

    void clear_stall_records()
    {
        m_arch_core.reset_stall_records();
        m_arch_stall_pending = 0;
        m_arch_stall_event_buffer.clear();
        m_arch_stall_buffered = 0;
        m_arch_stall_suppressed = 0;
        m_arch_stall_merged = 0;
        m_arch_early_retry_attempted = 0;
        m_arch_early_retry_succeeded = 0;
        m_arch_early_retry_failed = 0;
        m_arch_early_retry_discarded = 0;
        clear_endpoint_replay_records();
    }

    void clear_endpoint_replay_records()
    {
        m_arch_core.reset_endpoint_replay_records();
        m_arch_endpoint_replay_pending = 0;
    }

    uint32_t arch_event_class_for_fault(uint32_t reason) const
    {
        return apollo::smmuv3::apollo_smmu_arch_core::event_class_for_fault(
            reason, m_arch_fault_stage, m_arch_fault_event_class);
    }

    uint64_t arch_event_record_word1(uint16_t stag, bool stall, bool write,
                                     bool privileged, bool instruction) const
    {
        return m_arch_core.event_record_word1(m_arch_fault_reason, stag, stall, write,
                                              privileged, instruction);
    }

    static uint32_t arch_event_number_for_fault(uint32_t reason)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::event_number_for_fault(reason);
    }

    uint64_t arch_event_record_word0(uint32_t stream_id, uint32_t event_number) const
    {
        const bool ssv = m_arch_current_ssid_valid || m_arch_selected_ssid_valid;
        const uint32_t ssid = m_arch_current_ssid_valid ? m_arch_current_ssid :
                                                              m_arch_selected_ssid;

        return arch_event_record_word0(stream_id, event_number, ssv, ssid);
    }

    static uint64_t arch_event_record_word0(uint32_t stream_id, uint32_t event_number,
                                            bool ssid_valid, uint32_t ssid)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::event_record_word0(
            stream_id, event_number, ssid_valid, ssid);
    }

    static bool arch_event_record_has_res0_payload(uint32_t event_number)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::event_record_has_res0_payload(
            event_number);
    }

    static bool arch_event_record_has_fetch_reason(uint32_t event_number)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::event_record_has_fetch_reason(
            event_number);
    }

    static bool arch_event_record_has_conflict_reason(uint32_t event_number)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::event_record_has_conflict_reason(
            event_number);
    }

    uint64_t arch_event_record_nonstall_word1(uint32_t event_number, uint64_t iova) const
    {
        return m_arch_core.event_record_nonstall_word1(m_arch_fault_reason, event_number,
                                                       iova);
    }

    uint64_t arch_event_record_word2(uint32_t event_number, uint64_t len) const
    {
        return m_arch_core.event_record_word2(m_arch_fault_reason, event_number, len);
    }

    uint64_t arch_event_record_word3(uint32_t event_number) const
    {
        return m_arch_core.event_record_word3(event_number);
    }

    void push_event_record(uint32_t stream_id, const char* reason, uint64_t iova, uint64_t len,
                           bool stall, bool write)
    {
        const bool ssid_valid = m_arch_current_ssid_valid || m_arch_selected_ssid_valid;
        const uint32_t ssid = m_arch_current_ssid_valid ? m_arch_current_ssid :
                                                              m_arch_selected_ssid;

        push_event_record(stream_id, reason, iova, len, stall, write, ssid_valid, ssid, 0,
                          false, false);
    }

    void push_event_record(uint32_t stream_id, const char* reason, uint64_t iova, uint64_t len,
                           bool stall, bool write, bool ssid_valid, uint32_t ssid,
                           uint16_t stag, bool privileged = false,
                           bool instruction = false)
    {
        const auto layout = m_arch_core.build_event_record(
            stream_id, m_arch_fault_reason, iova, len, stall, write, ssid_valid, ssid,
            stag, privileged, instruction);
        const uint32_t detail = layout.detail;
        const uint32_t event_number = layout.event_number;
        const uint64_t detail64 = layout.detail64;
        const auto& words = layout.words;

        m_arch_last_fault_detail = detail;
        if (!arch_eventq_enabled(stall, layout.smmu_disabled_recordable)) {
            SCP_INFO(()) << "APOLLO_SMMU_TBU: EVENTQ gated by CR0"
                         << " stream-id=0x" << std::hex << stream_id
                         << " event=0x" << event_number << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: EVENTQ gated by CR0"
                      << " stream-id=0x" << std::hex << stream_id
                      << " event=0x" << event_number << std::dec << std::endl;
            return;
        }
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected EVENTQ record layout"
                     << " event=0x" << std::hex << event_number
                     << " stream-id=0x" << stream_id
                     << " ssid=0x" << ((words[0] >> 12) & ARCH_CMDQ_SSID_MASK)
                     << " ssv=" << ((words[0] >> 11) & 0x1)
                     << " input=0x" << iova << " word2=0x" << words[2]
                     << " stag=0x" << stag
                     << " detail=0x" << detail64 << " reason=" << reason << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected EVENTQ record layout"
                  << " event=0x" << std::hex << event_number
                  << " stream-id=0x" << stream_id
                  << " ssid=0x" << ((words[0] >> 12) & ARCH_CMDQ_SSID_MASK)
                  << " ssv=" << ((words[0] >> 11) & 0x1)
                  << " input=0x" << iova << " word2=0x" << words[2]
                  << " stag=0x" << stag
                  << " detail=0x" << detail64 << " reason=" << reason << std::dec
                  << std::endl;
        m_arch_next_fault_replay_id++;
        /*
         * Arm SMMUv3 requires stalled fault records to be retained when the
         * Event queue is full and made visible once software frees an entry.
         * Non-stall events still use the normal overflow path below.
         */
        arch_queue& target_eventq =
            arch_eventq_for_security_state(m_arch_last_security_state);
        if (stall && queue_entries(target_eventq) != 0 &&
            queue_base_addr(target_eventq) != 0 &&
            arch_queue_would_overflow(target_eventq)) {
            buffer_stall_event_record(words, m_arch_last_security_state);
            return;
        }
        const uint64_t guest_record_addr =
            arch_queue_next_record_addr(target_eventq, ARCH_EVENTQ_ENTRY_BYTES);
        if (push_arch_queue_record(target_eventq, "EVENTQ", ARCH_EVENTQ_ENTRY_BYTES, words,
                                   words.size(), ARCH_GERROR_EVENTQ_ABORT,
                                   m_arch_last_security_state)) {
            record_arch_eventq_security_route(m_arch_last_security_state, words,
                                             guest_record_addr, queue_prod_reg(target_eventq),
                                             queue_cons_reg(target_eventq));
            if (stall) {
                mark_stall_event_committed(stream_id, stag);
            }
            set_arch_irq_status_for_security_state(ARCH_IRQ_EVENTQ,
                                                   m_arch_last_security_state);
        }
    }

    void push_event_record(uint32_t stream_id, const char* reason, uint64_t iova, uint64_t len)
    {
        push_event_record(stream_id, reason, iova, len, false, false);
    }

    void push_event_record(const char* reason, uint64_t iova, uint64_t len)
    {
        push_event_record(default_stream_id(), reason, iova, len);
    }

    void push_pri_record(uint32_t stream_id, uint64_t iova, uint64_t pa, uint64_t len)
    {
        const uint8_t security_state = m_arch_last_security_state;
        if (!arch_priq_enabled_for_security_state(security_state)) {
            SCP_INFO(()) << "APOLLO_SMMU_TBU: PRIQ gated by CR0"
                         << " stream-id=0x" << std::hex << stream_id
                         << " iova=0x" << iova << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: PRIQ gated by CR0"
                      << " stream-id=0x" << std::hex << stream_id
                      << " iova=0x" << iova << std::dec << std::endl;
            return;
        }
        const std::array<uint64_t, 4> words {{
            (static_cast<uint64_t>(stream_id) << 32) | 0x2u,
            iova,
            pa,
            len,
        }};

        auto& priq = arch_priq_for_security_state(security_state);
        const char* const queue_name =
            arch_security_uses_secure_irq_bank(security_state) ? "S_PRIQ" : "PRIQ";
        if (push_arch_queue_record(priq, queue_name, ARCH_PRIQ_ENTRY_BYTES, words,
                                   words.size(), ARCH_GERROR_PRIQ_ABORT,
                                   security_state)) {
            set_arch_irq_status_for_security_state(ARCH_IRQ_PRIQ, security_state);
        }
    }

    static uint64_t arch_priq_ppr_word0(uint32_t stream_id, bool ssid_valid,
                                        bool last, bool write, bool read,
                                        bool execute, bool privileged)
    {
        return (static_cast<uint64_t>(stream_id) << 32) |
               ARCH_PRIQ_PPR_TYPE |
               (ssid_valid ? ARCH_PRIQ_PPR_SSV : 0) |
               (last ? ARCH_PRIQ_PPR_LAST : 0) |
               (write ? ARCH_PRIQ_PPR_WRITE : 0) |
               (read ? ARCH_PRIQ_PPR_READ : 0) |
               (execute ? ARCH_PRIQ_PPR_EXEC : 0) |
               (privileged ? ARCH_PRIQ_PPR_PRIV : 0);
    }

    static uint64_t arch_priq_ppr_word3(uint16_t prg, bool ssid_valid,
                                        uint32_t ssid, uint64_t len)
    {
        return (static_cast<uint64_t>(arch_prg_index(prg)) << 48) |
               (ssid_valid ? ((static_cast<uint64_t>(ssid) &
                                ARCH_PRIQ_PPR_SSID_MASK)
                               << ARCH_PRIQ_PPR_SSID_SHIFT)
                           : 0) |
               (len & ARCH_PRIQ_PPR_LEN_MASK);
    }

    static bool arch_priq_ppr_is_stop_marker(bool ssid_valid, bool last,
                                             bool write, bool read)
    {
        /*
         * SMMUv3 PRI Stop PASID Markers are PPRs with SSV==1 and
         * LWR==0b100.  They delimit PASID traffic and never generate PRG
         * responses, including when they are discarded.
         */
        return ssid_valid && last && !write && !read;
    }

    static bool arch_pri_secure_stream_auto_failure(uint8_t security_state)
    {
        /*
         * SMMUv3 PRI miscellaneous rules require all incoming PPRs from a
         * Secure stream to receive Response Failure.  Keep this on the
         * protocol PPR path so the older Secure PRIQ bank compatibility helper
         * can still exercise Secure queue/MSI plumbing separately.
         */
        return arch_security_eventq_index(security_state) == ARCH_SECURITY_SECURE;
    }

    bool arch_pri_ppar_lookup_fault_recordable() const
    {
        if (m_arch_fault_reason == ARCH_FAULT_BAD_STREAM_ID) {
            return arch_record_bad_streamid_ats_treq();
        }

        return arch_record_translated_config_fault();
    }

    void record_pri_ppar_lookup_fault(uint32_t stream_id, uint64_t iova,
                                      uint64_t len)
    {
        if (!arch_pri_ppar_lookup_fault_recordable()) {
            m_arch_fault_record_suppressed = true;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected PRI STE.PPAR"
                         << " lookup fault suppressed"
                         << " stream-id=0x" << std::hex << stream_id
                         << " reason=0x" << m_arch_fault_reason
                         << " cr2=0x" << m_arch_cr2 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected PRI STE.PPAR"
                      << " lookup fault suppressed"
                      << " stream-id=0x" << std::hex << stream_id
                      << " reason=0x" << m_arch_fault_reason
                      << " cr2=0x" << m_arch_cr2 << std::dec << std::endl;
            return;
        }

        m_arch_pri_ppar_lookup_fault_records++;
        record_fault(stream_id, "pri-ppar-lookup", iova, len);
    }

    uint8_t arch_pri_overflow_auto_response(uint32_t stream_id, bool ssid_valid,
                                            uint32_t ssid, uint64_t iova,
                                            uint64_t len, bool& response_ssv,
                                            uint32_t& response_ssid)
    {
        /*
         * SMMUv3 PRI queue overflow auto-responses always report Success for
         * Last PPRs without PASID.  For PASID-prefixed PPRs, IDR3.PPS can force
         * the response to carry the same PASID; otherwise the associated
         * STE.PPAR bit controls whether the auto-response keeps or drops PASID.
         * A failed STE.PPAR lookup is the only overflow case modeled here that
         * returns Response Failure.
         */
        response_ssv = false;
        response_ssid = 0;
        m_arch_last_pri_pps = (ARCH_IDR3 & ARCH_IDR3_PPS) != 0;
        m_arch_last_pri_ppar = false;

        if (!ssid_valid) {
            return ARCH_PRI_RESP_ACCEPT;
        }
        if (m_arch_last_pri_pps) {
            response_ssv = true;
            response_ssid = ssid & ARCH_PRIQ_PPR_SSID_MASK;
            return ARCH_PRI_RESP_ACCEPT;
        }

        uint64_t ste0 = 0;
        uint64_t ste1 = 0;
        uint64_t ste2 = 0;
        uint64_t ste_pa = 0;
        m_arch_pri_auto_ste_ppar_checks++;
        const bool ste_lookup_ok =
            read_arch_ste_words(stream_id, ste0, ste1, ste2, ste_pa);
        if (!ste_lookup_ok || (ste0 & ARCH_STE_VALID) == 0 ||
            !arch_ste_config_supported(ste0)) {
            if (ste_lookup_ok) {
                m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            }
            m_arch_pri_auto_ste_ppar_failures++;
            record_pri_ppar_lookup_fault(stream_id, iova, len);
            return ARCH_PRI_RESP_FAILURE;
        }

        m_arch_last_pri_ppar = arch_ste_ppar(ste1);
        if (m_arch_last_pri_ppar) {
            response_ssv = true;
            response_ssid = ssid & ARCH_PRIQ_PPR_SSID_MASK;
        }
        return ARCH_PRI_RESP_ACCEPT;
    }

    void push_pri_protocol_record(uint32_t stream_id, uint16_t prg, uint8_t ats_status,
                                  uint64_t iova, uint64_t pa, uint64_t len,
                                  bool ssid_valid = false, uint32_t ssid = 0,
                                  bool last = true, bool read = true,
                                  bool write = false, bool execute = false,
                                  bool privileged = false)
    {
        const uint8_t security_state = m_arch_last_security_state;
        const bool stop_marker =
            arch_priq_ppr_is_stop_marker(ssid_valid, last, write, read);
        const uint64_t ppr_word0 =
            arch_priq_ppr_word0(stream_id, ssid_valid, last, write, read,
                                execute, privileged);
        const uint64_t ppr_word3 =
            arch_priq_ppr_word3(prg, ssid_valid, ssid, len);

        m_arch_last_pri_ppr_word0 = ppr_word0;
        m_arch_last_pri_ppr_word3 = ppr_word3;
        if (prg != 0) {
            for (auto& request : m_arch_pri_pending) {
                if (request.pending && request.prg == prg) {
                    request.security_state = security_state;
                    request.ssid_valid = ssid_valid;
                    request.ssid = ssid_valid ? (ssid & ARCH_CMDQ_SSID_MASK) : 0;
                    break;
                }
            }
        }
        if (stop_marker) {
            m_arch_pri_stop_markers++;
            clear_pending_prg(prg);
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected PRI Stop PASID Marker"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ssid=0x" << ssid
                         << " prg=0x" << prg << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected PRI Stop PASID Marker"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ssid=0x" << ssid
                      << " prg=0x" << prg << std::dec << std::endl;
            return;
        }

        if (arch_pri_secure_stream_auto_failure(security_state)) {
            m_arch_pri_secure_auto_failures++;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected PRI secure-stream"
                         << " auto-failure"
                         << " stream-id=0x" << std::hex << stream_id
                         << " prg=0x" << prg << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected PRI secure-stream"
                      << " auto-failure"
                      << " stream-id=0x" << std::hex << stream_id
                      << " prg=0x" << prg << std::dec << std::endl;
            record_pri_auto_response(stream_id, prg, ARCH_PRI_RESP_FAILURE,
                                     "secure-stream-pri");
            return;
        }

        /*
         * Incoming PRI Page Request messages are a PRI-side protocol input.
         * Per the SMMUv3 ATS/PRI rules they are not gated by CR0.ATSCHK or
         * the selected STE.EATS value; those fields gate ATS Translation
         * Requests/Translated transactions.  Keep the normal PRIQ enqueue
         * path configuration-agnostic, and only consult STE.PPAR on the
         * architected overflow auto-response path below.
         */
        if (!arch_priq_enabled_for_security_state(security_state)) {
            SCP_INFO(()) << "APOLLO_SMMU_TBU: PRIQ protocol record gated by CR0"
                         << " stream-id=0x" << std::hex << stream_id
                         << " prg=0x" << prg << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: PRIQ protocol record gated by CR0"
                      << " stream-id=0x" << std::hex << stream_id
                      << " prg=0x" << prg << std::dec << std::endl;
            record_pri_auto_response(stream_id, prg, ARCH_PRI_RESP_FAILURE,
                                     "priq-disabled");
            return;
        }
        if (arch_priq_abort_active(security_state)) {
            record_pri_auto_response(stream_id, prg, ARCH_PRI_RESP_FAILURE,
                                     "priq-abort-active");
            return;
        }
        /*
         * Keep word0 compatible with the existing guest selftest, which only
         * validates the low record type bits. Carry the protocol slice fields
         * in later words so PRG/status are still visible in memory-backed PRIQ
         * records without breaking the compatibility ABI.
         */
        const std::array<uint64_t, 4> words {{
            ppr_word0,
            iova,
            (static_cast<uint64_t>(ats_status) << 56) | (pa & 0x00ffffffffffffffULL),
            ppr_word3,
        }};

        auto& priq = arch_priq_for_security_state(security_state);
        const char* const queue_name =
            arch_security_uses_secure_irq_bank(security_state) ? "S_PRIQ" : "PRIQ";
        if (push_arch_queue_record(priq, queue_name, ARCH_PRIQ_ENTRY_BYTES, words,
                                   words.size(), ARCH_GERROR_PRIQ_ABORT,
                                   security_state)) {
            set_arch_irq_status_for_security_state(ARCH_IRQ_PRIQ, security_state);
        } else if (last) {
            const bool overflow_active = priq.overflow;
            bool response_ssv = false;
            uint32_t response_ssid = 0;
            const uint8_t response =
                overflow_active ?
                    arch_pri_overflow_auto_response(stream_id, ssid_valid, ssid,
                                                    iova, len, response_ssv,
                                                    response_ssid) :
                    ARCH_PRI_RESP_FAILURE;
            record_pri_auto_response(stream_id, prg, response,
                                     overflow_active ? "priq-overflow" :
                                                       "priq-unwritable",
                                     response_ssv, response_ssid);
        } else {
            m_arch_pri_discarded_nonlast++;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected PRI non-last PPR"
                         << " discarded without auto-response"
                         << " stream-id=0x" << std::hex << stream_id
                         << " prg=0x" << prg << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected PRI non-last PPR"
                      << " discarded without auto-response"
                      << " stream-id=0x" << std::hex << stream_id
                      << " prg=0x" << prg << std::dec << std::endl;
        }
    }

    void push_pri_record(uint64_t iova, uint64_t pa, uint64_t len)
    {
        push_pri_record(default_stream_id(), iova, pa, len);
    }

    static uint32_t cmdq_stream_id(uint64_t word0)
    {
        return static_cast<uint32_t>(word0 >> 32);
    }

    static uint64_t cmdq_page(uint64_t word1)
    {
        return page_base(word1);
    }

    static uint16_t cmdq_tlbi_asid(uint64_t word0)
    {
        return static_cast<uint16_t>((word0 >> ARCH_CMDQ_TLBI_ASID_SHIFT) &
                                     ARCH_CMDQ_TAG_MASK);
    }

    static uint16_t cmdq_tlbi_vmid(uint64_t word0)
    {
        return static_cast<uint16_t>((word0 >> ARCH_CMDQ_TLBI_VMID_SHIFT) &
                                     ARCH_CMDQ_TAG_MASK);
    }

    static uint32_t cmdq_range_num(uint64_t word0)
    {
        return static_cast<uint32_t>((word0 >> ARCH_CMDQ_RANGE_NUM_SHIFT) &
                                     ARCH_CMDQ_RANGE_NUM_MASK);
    }

    static uint32_t cmdq_range_scale(uint64_t word0)
    {
        return static_cast<uint32_t>((word0 >> ARCH_CMDQ_RANGE_SCALE_SHIFT) &
                                     ARCH_CMDQ_RANGE_SCALE_MASK);
    }

    static uint32_t cmdq_range_tg(uint64_t word1)
    {
        return static_cast<uint32_t>((word1 >> ARCH_CMDQ_RANGE_TG_SHIFT) &
                                     ARCH_CMDQ_RANGE_TG_MASK);
    }

    static uint32_t cmdq_tlbi_ttl(uint64_t word1)
    {
        return static_cast<uint32_t>((word1 >> ARCH_CMDQ_TTL_SHIFT) &
                                     ARCH_CMDQ_TTL_MASK);
    }

    static bool cmdq_tlbi_leaf(uint64_t word1)
    {
        return (word1 & ARCH_CMDQ_LEAF) != 0;
    }

    static bool cmdq_opcode_is_address_tlbi(uint32_t opcode)
    {
        switch (opcode) {
        case ARCH_CMD_TLBI_NH_VA:
        case ARCH_CMD_TLBI_NH_VAA:
        case ARCH_CMD_TLBI_EL3_VA:
        case ARCH_CMD_TLBI_EL2_VA:
        case ARCH_CMD_TLBI_EL2_VAA:
        case ARCH_CMD_TLBI_S2_IPA:
        case ARCH_CMD_TLBI_S_EL2_VA:
        case ARCH_CMD_TLBI_S_EL2_VAA:
        case ARCH_CMD_TLBI_S_S2_IPA:
            return true;
        default:
            return false;
        }
    }

    static bool cmdq_opcode_is_secure_tlbi(uint32_t opcode)
    {
        switch (opcode) {
        case ARCH_CMD_TLBI_S_EL2_ALL:
        case ARCH_CMD_TLBI_S_EL2_ASID:
        case ARCH_CMD_TLBI_S_EL2_VA:
        case ARCH_CMD_TLBI_S_EL2_VAA:
        case ARCH_CMD_TLBI_S_S12_VMALL:
        case ARCH_CMD_TLBI_S_S2_IPA:
        case ARCH_CMD_TLBI_SNH_ALL:
            return true;
        default:
            return false;
        }
    }

    static uint64_t cmdq_range_granule_bytes(uint32_t tg)
    {
        switch (tg) {
        case ARCH_CMDQ_RANGE_TG_16K:
            return 16ULL * 1024ULL;
        case ARCH_CMDQ_RANGE_TG_64K:
            return 64ULL * 1024ULL;
        case ARCH_CMDQ_RANGE_TG_4K:
        default:
            return 4ULL * 1024ULL;
        }
    }

    static uint64_t cmdq_tlbi_range_bytes(uint64_t word0, uint64_t word1)
    {
        const uint32_t tg = cmdq_range_tg(word1);

        if (tg == 0) {
            return PAGE_SIZE;
        }

        uint64_t span = static_cast<uint64_t>(cmdq_range_num(word0)) + 1ULL;
        const uint32_t scale = cmdq_range_scale(word0);
        const uint64_t granule = cmdq_range_granule_bytes(tg);

        if (scale >= 63 || span > (UINT64_MAX / granule)) {
            return UINT64_MAX;
        }
        span *= granule;
        if (span > (UINT64_MAX >> scale)) {
            return UINT64_MAX;
        }
        return span << scale;
    }

    static bool cmdq_tlbi_range_encoding_reserved(uint64_t word0, uint64_t word1)
    {
        return cmdq_range_tg(word1) != 0 &&
               cmdq_range_num(word0) == 0 &&
               cmdq_range_scale(word0) == 0;
    }

    static bool cmdq_ssid_valid(uint64_t word0)
    {
        return (word0 & ARCH_CMDQ_SSV) != 0;
    }

    static uint32_t cmdq_ssid(uint64_t word0)
    {
        return static_cast<uint32_t>((word0 >> ARCH_CMDQ_SSID_SHIFT) &
                                     ARCH_CMDQ_SSID_MASK);
    }

    static bool cmdq_ssec(uint64_t word0)
    {
        return (word0 & ARCH_CMDQ_SSEC) != 0;
    }

    static bool cmdq_opcode_has_ssec(uint32_t opcode)
    {
        switch (opcode) {
        case ARCH_CMD_CFGI_STE:
        case ARCH_CMD_CFGI_ALL:
        case ARCH_CMD_CFGI_CD:
        case ARCH_CMD_CFGI_CD_ALL:
        case ARCH_CMD_CFGI_VMS_PIDM:
        case ARCH_CMD_TLBI_NH_ALL:
        case ARCH_CMD_TLBI_NH_ASID:
        case ARCH_CMD_TLBI_NH_VA:
        case ARCH_CMD_TLBI_NH_VAA:
        case ARCH_CMD_TLBI_EL3_ALL:
        case ARCH_CMD_TLBI_EL3_VA:
        case ARCH_CMD_TLBI_EL2_ALL:
        case ARCH_CMD_TLBI_EL2_ASID:
        case ARCH_CMD_TLBI_EL2_VA:
        case ARCH_CMD_TLBI_EL2_VAA:
        case ARCH_CMD_TLBI_S12_VMALL:
        case ARCH_CMD_TLBI_S2_IPA:
        case ARCH_CMD_TLBI_NSNH_ALL:
        case ARCH_CMD_ATC_INV:
        case ARCH_CMD_RESUME:
        case ARCH_CMD_STALL_TERM:
            return true;
        default:
            return false;
        }
    }

    static uint8_t secure_cmdq_ssec_state(uint64_t word0)
    {
        return cmdq_ssec(word0) ? ARCH_SECURITY_SECURE : ARCH_SECURITY_NONSECURE;
    }

    static uint8_t secure_cmdq_command_security_state(uint32_t opcode, uint64_t word0)
    {
        /*
         * IHI0070G.b §4.5.2: when CMD_PRI_RESP is accepted from the Secure
         * command queue, the command StreamID is considered Non-secure.  The
         * CMD_PRI_RESP encoding has no SSec selector, so ignore bit[10] here
         * even if a test or guest writes it as a RES0/reserved bit.
         */
        if (opcode == ARCH_CMD_PRI_RESP) {
            return ARCH_SECURITY_NONSECURE;
        }

        return cmdq_opcode_is_secure_tlbi(opcode) ?
                   ARCH_SECURITY_SECURE :
                   secure_cmdq_ssec_state(word0);
    }

    static bool cmdq_atc_global(uint64_t word0)
    {
        return (word0 & (1ULL << 9)) != 0;
    }

    static uint32_t cmdq_atc_size(uint64_t word1)
    {
        return static_cast<uint32_t>(word1 & 0x3f);
    }

    void record_cmdq_decoded(uint32_t opcode,
                             uint8_t security_state = ARCH_SECURITY_NONSECURE,
                             bool ssec = false)
    {
        m_arch_cmd_processed++;
        m_arch_last_cmd_opcode = opcode;
        m_arch_last_cmd_stream_id = 0;
        m_arch_last_cmd_asid = 0;
        m_arch_last_cmd_vmid = 0;
        m_arch_last_cmd_ssid = 0;
        m_arch_last_cmd_ssid_valid = false;
        m_arch_last_cmd_iova = 0;
        m_arch_last_cmd_invalidated = 0;
        m_arch_last_cmd_security_state = security_state;
        m_arch_last_cmd_ssec = ssec;
        m_arch_last_cmd_range_bytes = 0;
        m_arch_last_cmd_table_invalidated = 0;
        m_arch_last_cmd_tg = 0;
        m_arch_last_cmd_ttl = 0;
        m_arch_last_cmd_leaf = false;
    }

    void record_cmdq_invalidation(const char* op_name, uint32_t stream_id, uint64_t iova,
                                  uint32_t invalidated, uint16_t asid = 0,
                                  uint16_t vmid = 0, bool ssid_valid = false,
                                  uint32_t ssid = 0,
                                  uint8_t security_state = ARCH_SECURITY_NONSECURE)
    {
        m_arch_cmd_invalidations += invalidated;
        m_arch_last_cmd_stream_id = stream_id;
        m_arch_last_cmd_asid = asid;
        m_arch_last_cmd_vmid = vmid;
        m_arch_last_cmd_ssid = ssid;
        m_arch_last_cmd_ssid_valid = ssid_valid;
        m_arch_last_cmd_iova = iova;
        m_arch_last_cmd_invalidated = invalidated;
        m_arch_last_cmd_security_state = security_state;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected CMDQ invalidation op=" << op_name
                     << " stream-id=0x" << std::hex << stream_id << " iova=0x" << iova
                     << " asid=0x" << asid << " vmid=0x" << vmid
                     << " security-state=0x"
                     << static_cast<uint32_t>(security_state);
        if (ssid_valid) {
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected CMDQ invalidation ssid=0x"
                         << std::hex << ssid;
        }
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected CMDQ invalidation result"
                     << " invalidated=" << std::dec << invalidated
                     << " ats-entries=" << m_ats_entries;
        std::cerr << "APOLLO_SMMU_TBU: architected CMDQ invalidation op=" << op_name
                  << " stream-id=0x" << std::hex << stream_id << " iova=0x" << iova
                  << " asid=0x" << asid << " vmid=0x" << vmid
                  << " security-state=0x"
                  << static_cast<uint32_t>(security_state);
        if (ssid_valid) {
            std::cerr << " ssid=0x" << ssid;
        }
        std::cerr
                  << " invalidated=" << std::dec << invalidated
                  << " ats-entries=" << m_ats_entries;
        if (m_arch_last_cmd_table_invalidated != 0) {
            std::cerr << " table-invalidated="
                      << m_arch_last_cmd_table_invalidated;
        }
        std::cerr << std::endl;
    }

    uint32_t clear_modeled_vms_state()
    {
        const uint32_t invalidated = (m_arch_last_ste5 != 0 || m_arch_last_vms_ptr != 0) ? 1u : 0;

        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_vms_partid_map.fill(0);
        return invalidated;
    }

    uint32_t clear_modeled_vms_state_for_stream(uint32_t stream_id, bool all)
    {
        if (!all && stream_id != selected_arch_stream_id()) {
            return 0;
        }
        return clear_modeled_vms_state();
    }

    uint32_t clear_modeled_vms_state_for_vmid(uint16_t vmid)
    {
        if (m_arch_last_vms_ptr == 0 && m_arch_last_ste5 == 0) {
            return 0;
        }
        if (vmid != m_arch_last_vmid) {
            return 0;
        }
        return clear_modeled_vms_state();
    }

    static const char* cmdq_cfgi_name(uint32_t opcode)
    {
        switch (opcode) {
        case ARCH_CMD_CFGI_STE:
            return "CFGI_STE";
        case ARCH_CMD_CFGI_CD:
            return "CFGI_CD";
        case ARCH_CMD_CFGI_CD_ALL:
            return "CFGI_CD_ALL";
        case ARCH_CMD_CFGI_ALL:
            return "CFGI_ALL";
        default:
            return "CFGI";
        }
    }

    void handle_cmdq_cfgi(uint64_t word0, bool all,
                          uint8_t security_state = ARCH_SECURITY_NONSECURE)
    {
        const uint32_t opcode = static_cast<uint32_t>(word0 & 0xffu);
        const uint32_t stream_id = cmdq_stream_id(word0);
        uint32_t invalidated = 0;

        m_arch_cmd_cfgis++;
        if (all) {
            invalidated = clear_ats_cache();
            invalidated += clear_config_cache_security_state(security_state);
            if (m_arch_last_security_state == security_state) {
                m_arch_last_ste = 0;
                m_arch_last_cd = 0;
            }
            invalidated += clear_modeled_vms_state_for_stream(stream_id, true);
        } else {
            invalidated = clear_ats_cache(stream_id);
            invalidated += clear_config_cache_span(stream_id, security_state, 0);
            if (stream_id == selected_arch_stream_id() &&
                m_arch_last_security_state == security_state) {
                m_arch_last_ste = 0;
                m_arch_last_cd = 0;
                if (opcode == ARCH_CMD_CFGI_STE) {
                    invalidated += clear_modeled_vms_state_for_stream(stream_id, false);
                }
            }
        }
        record_cmdq_invalidation(cmdq_cfgi_name(opcode), stream_id, 0, invalidated,
                                 0, 0, false, 0, security_state);
    }

    void handle_cmdq_cfgi_vms_pidm(uint64_t word0,
                                   uint8_t security_state = ARCH_SECURITY_NONSECURE)
    {
        const uint16_t vmid = cmdq_tlbi_vmid(word0);
        const uint32_t invalidated = clear_modeled_vms_state_for_vmid(vmid);

        m_arch_cmd_cfgis++;
        record_cmdq_invalidation("CFGI_VMS_PIDM", 0, 0, invalidated, 0, vmid,
                                 false, 0, security_state);
    }

    void handle_cmdq_tlbi(uint32_t opcode, uint64_t word0, uint64_t word1)
    {
        handle_cmdq_tlbi(opcode, word0, word1, ARCH_SECURITY_NONSECURE);
    }

    void handle_cmdq_tlbi(uint32_t opcode, uint64_t word0, uint64_t word1,
                          uint8_t security_state)
    {
        uint32_t invalidated = 0;
        const bool address_tlbi = cmdq_opcode_is_address_tlbi(opcode);
        const uint16_t asid = cmdq_tlbi_asid(word0);
        const uint16_t vmid = cmdq_tlbi_vmid(word0);
        const uint32_t tg = address_tlbi ? cmdq_range_tg(word1) : 0;
        const uint64_t page = address_tlbi ? cmdq_page(word1) : 0;
        const uint64_t range_bytes = address_tlbi ?
                                         cmdq_tlbi_range_bytes(word0, word1) :
                                         0;
        const char* op_name = "TLBI_NH_ALL";

        m_arch_cmd_tlbis++;
        m_arch_last_cmd_range_bytes = range_bytes;
        m_arch_last_cmd_tg = tg;
        m_arch_last_cmd_ttl = (address_tlbi && tg != 0) ?
                                  cmdq_tlbi_ttl(word1) :
                                  0;
        m_arch_last_cmd_leaf = address_tlbi && cmdq_tlbi_leaf(word1);
        if (address_tlbi && !m_arch_last_cmd_leaf) {
            m_arch_last_cmd_table_invalidated = 1;
            m_arch_cmd_table_invalidations += m_arch_last_cmd_table_invalidated;
        }
        switch (opcode) {
        case ARCH_CMD_TLBI_NSNH_ALL:
            invalidated = clear_ats_cache_nsnh(security_state);
            op_name = "TLBI_NSNH_ALL";
            break;
        case ARCH_CMD_TLBI_SNH_ALL:
            invalidated = clear_ats_cache(security_state);
            op_name = "TLBI_SNH_ALL";
            break;
        case ARCH_CMD_TLBI_EL3_ALL:
            invalidated = clear_ats_cache(security_state);
            op_name = "TLBI_EL3_ALL";
            break;
        case ARCH_CMD_TLBI_S_EL2_ALL:
        case ARCH_CMD_TLBI_EL2_ALL:
            invalidated = clear_ats_cache_vmid(vmid, security_state);
            op_name = opcode == ARCH_CMD_TLBI_S_EL2_ALL ?
                          "TLBI_S_EL2_ALL" :
                          "TLBI_EL2_ALL";
            break;
        case ARCH_CMD_TLBI_S_EL2_ASID:
        case ARCH_CMD_TLBI_EL2_ASID:
        case ARCH_CMD_TLBI_NH_ASID:
            invalidated = clear_ats_cache_asid(asid, vmid, security_state);
            op_name = opcode == ARCH_CMD_TLBI_S_EL2_ASID ?
                          "TLBI_S_EL2_ASID" :
                          (opcode == ARCH_CMD_TLBI_EL2_ASID ?
                               "TLBI_EL2_ASID" :
                               "TLBI_NH_ASID");
            break;
        case ARCH_CMD_TLBI_EL3_VA:
            invalidated = clear_ats_cache_range_vmid(page, range_bytes, vmid,
                                                     security_state,
                                                     m_arch_last_cmd_ttl, tg);
            op_name = "TLBI_EL3_VA";
            break;
        case ARCH_CMD_TLBI_S_EL2_VA:
        case ARCH_CMD_TLBI_EL2_VA:
        case ARCH_CMD_TLBI_NH_VA:
            invalidated = clear_ats_cache_range_asid(page, range_bytes, asid,
                                                     vmid, security_state,
                                                     m_arch_last_cmd_ttl, tg);
            op_name = opcode == ARCH_CMD_TLBI_S_EL2_VA ?
                          "TLBI_S_EL2_VA" :
                          (opcode == ARCH_CMD_TLBI_EL2_VA ?
                               "TLBI_EL2_VA" :
                               "TLBI_NH_VA");
            break;
        case ARCH_CMD_TLBI_S_EL2_VAA:
        case ARCH_CMD_TLBI_EL2_VAA:
        case ARCH_CMD_TLBI_NH_VAA:
            invalidated = clear_ats_cache_range_vmid(page, range_bytes, vmid,
                                                     security_state,
                                                     m_arch_last_cmd_ttl, tg);
            op_name = opcode == ARCH_CMD_TLBI_S_EL2_VAA ?
                          "TLBI_S_EL2_VAA" :
                          (opcode == ARCH_CMD_TLBI_EL2_VAA ?
                               "TLBI_EL2_VAA" :
                               "TLBI_NH_VAA");
            break;
        case ARCH_CMD_TLBI_S_S12_VMALL:
        case ARCH_CMD_TLBI_S12_VMALL:
            invalidated = clear_ats_cache_vmid(vmid, security_state);
            op_name = opcode == ARCH_CMD_TLBI_S_S12_VMALL ?
                          "TLBI_S_S12_VMALL" :
                          "TLBI_S12_VMALL";
            break;
        case ARCH_CMD_TLBI_S_S2_IPA:
        case ARCH_CMD_TLBI_S2_IPA:
            invalidated = clear_ats_cache_range_vmid(page, range_bytes, vmid,
                                                     security_state,
                                                     m_arch_last_cmd_ttl, tg);
            op_name = opcode == ARCH_CMD_TLBI_S_S2_IPA ?
                          "TLBI_S_S2_IPA" :
                          "TLBI_S2_IPA";
            break;
        case ARCH_CMD_TLBI_NH_ALL:
        default:
            invalidated = clear_ats_cache_vmid(vmid, security_state);
            op_name = "TLBI_NH_ALL";
            break;
        }
        record_cmdq_invalidation(op_name, 0, page, invalidated, asid, vmid,
                                 false, 0, security_state);
    }

    void handle_cmdq_atc_inv(uint64_t word0, uint64_t word1)
    {
        handle_cmdq_atc_inv(word0, word1, ARCH_SECURITY_NONSECURE);
    }

    void handle_cmdq_atc_inv(uint64_t word0, uint64_t word1,
                             uint8_t security_state)
    {
        const uint32_t stream_id = cmdq_stream_id(word0);
        const uint64_t page = cmdq_page(word1);
        const bool global = cmdq_atc_global(word0) || cmdq_atc_size(word1) >= 52;
        const bool ssid_valid = cmdq_ssid_valid(word0);
        const uint32_t ssid = cmdq_ssid(word0);
        uint32_t invalidated = 0;

        m_arch_cmd_atc_invs++;
        bool force_fail = false;
        if (m_arch_atc_inv_sync_force_fail_count > 0) {
            m_arch_atc_inv_sync_force_fail_count--;
            force_fail = true;
        }
        if (m_arch_atc_inv_sync_force_fail) {
            m_arch_atc_inv_sync_force_fail = false;
            force_fail = true;
        }
        if (force_fail) {
            m_arch_atc_inv_sync_pending_count++;
            m_arch_atc_inv_sync_error_pending = true;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: architected CMDQ ATC_INV completion failure"
                         << " stream-id=0x" << std::hex << stream_id
                         << " iova=0x" << page
                         << " pending=0x" << m_arch_atc_inv_sync_pending_count << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected CMDQ ATC_INV completion failure"
                      << " stream-id=0x" << std::hex << stream_id
                      << " iova=0x" << page
                      << " pending=0x" << m_arch_atc_inv_sync_pending_count
                      << std::dec << std::endl;
        }
        if (global) {
            invalidated = clear_ats_cache(security_state);
        } else if (stream_id != 0) {
            invalidated = clear_ats_cache_page_ssid(stream_id, page, ssid_valid,
                                                    ssid, security_state);
        } else {
            invalidated = clear_ats_cache_page(page, security_state);
        }
        record_cmdq_invalidation("ATC_INV", stream_id, global ? 0 : page, invalidated, 0, 0,
                                  ssid_valid, ssid, security_state);
    }

    void handle_cmdq_dpti_unsupported(uint32_t opcode, uint64_t word1)
    {
        /*
         * This functional model reports IDR3.DPT=0, so architected DPT
         * maintenance commands must not be consumed as no-ops.  The pinned
         * SMMUv3 reference treats CMD_DPTI_ALL/CMD_DPTI_PA as CERROR_ILL
         * and signals a command queue error when dirty-page tracking is not
         * implemented.  Leave CMDQ_CONS.RD pointing at the failing command
         * and expose CERROR_ILL in CMDQ_CONS.ERR until software rewrites
         * CMDQ_CONS to clear/skip it.
         */
        m_arch_cmd_dptis++;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        m_arch_last_cmd_iova = cmdq_page(word1);
        set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL, "dpti-unsupported");
        SCP_WARN(()) << "APOLLO_SMMU_TBU: architected CMDQ DPTI unsupported"
                     << " op=0x" << std::hex << opcode
                     << " dpt=0 idr3=0x" << ARCH_IDR3
                     << " cerror=0x" << m_arch_cmdq_cerror
                     << " pa=0x" << m_arch_last_cmd_iova << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected CMDQ DPTI unsupported"
                  << " op=0x" << std::hex << opcode
                  << " dpt=0 idr3=0x" << ARCH_IDR3
                  << " cerror=0x" << m_arch_cmdq_cerror
                  << " pa=0x" << m_arch_last_cmd_iova << std::dec << std::endl;
    }

    void process_cmdq()
    {
        const uint32_t entries = queue_entries(m_cmdq);
        const uint64_t base = queue_base_addr(m_cmdq);
        uint32_t guard = entries;

        if (!arch_cmdq_enabled()) {
            log_arch_queue("CMDQ", "disabled", m_cmdq.prod, m_cmdq.cons);
            return;
        }

        if (m_arch_cmdq_cerror != ARCH_CMDQ_CERROR_NONE) {
            log_arch_queue("CMDQ", "cerror", m_cmdq.prod, read_cmdq_cons());
            return;
        }

        if (entries == 0 || base == 0) {
            set_cmdq_cerror(ARCH_CMDQ_CERROR_ABT, "unconfigured");
            log_arch_queue("CMDQ", "unconfigured", m_cmdq.prod, m_cmdq.cons);
            return;
        }

        while (m_cmdq.cons != m_cmdq.prod && guard-- > 0) {
            uint64_t word0 = 0;
            uint64_t word1 = 0;
            const uint64_t entry_pa = base + queue_index(m_cmdq.cons, entries) * ARCH_CMDQ_ENTRY_BYTES;

            if (!read_downstream_u64(entry_pa, word0, true) ||
                !read_downstream_u64(entry_pa + sizeof(word0), word1, true)) {
                set_cmdq_cerror(ARCH_CMDQ_CERROR_ABT, "fetch-failed");
                log_arch_queue("CMDQ", "fetch-failed", m_cmdq.prod, m_cmdq.cons);
                return;
            }

            const uint32_t opcode = static_cast<uint32_t>(word0 & 0xffu);
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected CMDQ op=0x" << std::hex << opcode
                         << " word0=0x" << word0 << " word1=0x" << word1 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected CMDQ op=0x" << std::hex << opcode
                      << " word0=0x" << word0 << " word1=0x" << word1 << std::dec << std::endl;

            const bool ssec = cmdq_ssec(word0);
            record_cmdq_decoded(opcode, ARCH_SECURITY_NONSECURE, ssec);
            bool command_ok = true;
            if (ssec && cmdq_opcode_has_ssec(opcode)) {
                set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL, "nonsecure-cmdq-ssec");
                command_ok = false;
            }
            if (command_ok && cmdq_opcode_is_secure_tlbi(opcode)) {
                set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                "nonsecure-secure-tlbi");
                command_ok = false;
            }
            if (command_ok) {
            switch (opcode) {
            case ARCH_CMD_SYNC:
                if (m_arch_atc_inv_sync_error_pending) {
                    m_arch_atc_inv_sync_pending_count = 0;
                    m_arch_atc_inv_sync_error_pending = false;
                    m_arch_atc_inv_sync_errors++;
                    set_cmdq_cerror(ARCH_CMDQ_CERROR_ATC_INV_SYNC, "atc-inv-sync");
                    command_ok = false;
                } else {
                    const uint32_t sync_cs =
                        static_cast<uint32_t>((word0 >> ARCH_CMD_SYNC_CS_SHIFT) &
                                              ARCH_CMD_SYNC_CS_MASK);
                    m_arch_last_cmd_sync_signal = sync_cs;
                    if (sync_cs == ARCH_CMD_SYNC_CS_RESERVED) {
                        set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL, "cmd-sync-reserved-cs");
                        command_ok = false;
                        break;
                    }
                    if (sync_cs == ARCH_CMD_SYNC_CS_IRQ) {
                        emit_arch_msi(ARCH_IRQ_CMDQ_SYNC, word1,
                                      static_cast<uint32_t>(word0 >> ARCH_CMD_SYNC_MSIDATA_SHIFT),
                                      ARCH_GERROR_MSI_CMDQ_ABORT);
                    }
                    m_arch_cmd_syncs++;
                    set_arch_irq_status(ARCH_IRQ_CMDQ_SYNC);
                }
                break;
            case ARCH_CMD_PRI_RESP:
            {
                const uint8_t pri_response =
                    static_cast<uint8_t>((word1 >> 16) & 0xfu);
                if (!arch_pri_supported()) {
                    set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                    "pri-resp-unsupported");
                    command_ok = false;
                    break;
                }
                if (!arch_pri_response_valid(pri_response)) {
                    set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                    "pri-resp-reserved-response");
                    command_ok = false;
                    break;
                }
                complete_prg(arch_prg_index(word1),
                             pri_response,
                             cmdq_stream_id(word0) != 0,
                             cmdq_stream_id(word0),
                             cmdq_ssid_valid(word0),
                             cmdq_ssid(word0));
                break;
            }
            case ARCH_CMD_RESUME:
                complete_stall(cmdq_stream_id(word0),
                               static_cast<uint16_t>(word1 & ARCH_CMD_RESUME_STAG_MASK),
                               static_cast<uint8_t>((word0 >> ARCH_CMD_RESUME_RESP_SHIFT) &
                                                    ARCH_CMD_RESUME_RESP_MASK));
                break;
            case ARCH_CMD_STALL_TERM:
                terminate_stalls_for_stream(cmdq_stream_id(word0));
                break;
            case ARCH_CMD_CFGI_STE:
            case ARCH_CMD_CFGI_CD:
            case ARCH_CMD_CFGI_CD_ALL:
                handle_cmdq_cfgi(word0, false, ARCH_SECURITY_NONSECURE);
                break;
            case ARCH_CMD_CFGI_ALL:
                handle_cmdq_cfgi(word0, true, ARCH_SECURITY_NONSECURE);
                break;
            case ARCH_CMD_CFGI_VMS_PIDM:
                handle_cmdq_cfgi_vms_pidm(word0, ARCH_SECURITY_NONSECURE);
                break;
            case ARCH_CMD_TLBI_NH_ALL:
            case ARCH_CMD_TLBI_NH_ASID:
            case ARCH_CMD_TLBI_NSNH_ALL:
            case ARCH_CMD_TLBI_EL3_ALL:
            case ARCH_CMD_TLBI_EL2_ALL:
            case ARCH_CMD_TLBI_EL2_ASID:
            case ARCH_CMD_TLBI_S12_VMALL:
                handle_cmdq_tlbi(opcode, word0, word1);
                break;
            case ARCH_CMD_TLBI_NH_VA:
            case ARCH_CMD_TLBI_NH_VAA:
            case ARCH_CMD_TLBI_EL3_VA:
            case ARCH_CMD_TLBI_EL2_VA:
            case ARCH_CMD_TLBI_EL2_VAA:
            case ARCH_CMD_TLBI_S2_IPA:
                if (cmdq_tlbi_range_encoding_reserved(word0, word1)) {
                    set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                    "tlbi-range-reserved");
                    command_ok = false;
                    break;
                }
                handle_cmdq_tlbi(opcode, word0, word1);
                break;
            case ARCH_CMD_ATC_INV:
                if (!arch_ats_supported()) {
                    set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                    "atc-inv-unsupported");
                    command_ok = false;
                    break;
                }
                handle_cmdq_atc_inv(word0, word1);
                break;
            case ARCH_CMD_DPTI_ALL:
            case ARCH_CMD_DPTI_PA:
                handle_cmdq_dpti_unsupported(opcode, word1);
                command_ok = false;
                break;
            default:
                m_arch_fault_reason = ARCH_FAULT_TABLE_INVALID;
                set_cmdq_cerror(ARCH_CMDQ_CERROR_ILL, "unsupported-cmd");
                command_ok = false;
                break;
            }
            }

            if (!command_ok) {
                break;
            }
            m_cmdq.cons = queue_advance(m_cmdq.cons, entries);
        }

        if (guard == 0 && m_cmdq.cons != m_cmdq.prod) {
            set_cmdq_cerror(ARCH_CMDQ_CERROR_ABT, "stalled");
            log_arch_queue("CMDQ", "stalled", m_cmdq.prod, m_cmdq.cons);
        }
    }

    void process_secure_cmdq()
    {
        const uint32_t entries = queue_entries(m_arch_secure_cmdq);
        const uint64_t base = queue_base_addr(m_arch_secure_cmdq);
        uint32_t guard = entries;

        if (!arch_secure_cmdq_enabled()) {
            log_arch_queue("S_CMDQ", "disabled", m_arch_secure_cmdq.prod,
                           m_arch_secure_cmdq.cons);
            return;
        }

        if (m_arch_secure_cmdq_cerror != ARCH_CMDQ_CERROR_NONE) {
            log_arch_queue("S_CMDQ", "cerror", m_arch_secure_cmdq.prod,
                           read_secure_cmdq_cons());
            return;
        }

        if (entries == 0 || base == 0) {
            set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ABT, "unconfigured");
            log_arch_queue("S_CMDQ", "unconfigured", m_arch_secure_cmdq.prod,
                           m_arch_secure_cmdq.cons);
            return;
        }

        while (m_arch_secure_cmdq.cons != m_arch_secure_cmdq.prod && guard-- > 0) {
            uint64_t word0 = 0;
            uint64_t word1 = 0;
            const uint64_t entry_pa =
                base + queue_index(m_arch_secure_cmdq.cons, entries) *
                           ARCH_CMDQ_ENTRY_BYTES;

            if (!read_downstream_u64(entry_pa, word0, true) ||
                !read_downstream_u64(entry_pa + sizeof(word0), word1, true)) {
                set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ABT, "fetch-failed");
                log_arch_queue("S_CMDQ", "fetch-failed", m_arch_secure_cmdq.prod,
                               m_arch_secure_cmdq.cons);
                return;
            }

            const uint32_t opcode = static_cast<uint32_t>(word0 & 0xffu);
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected S_CMDQ op=0x"
                         << std::hex << opcode << " word0=0x" << word0
                         << " word1=0x" << word1 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected S_CMDQ op=0x"
                      << std::hex << opcode << " word0=0x" << word0
                      << " word1=0x" << word1 << std::dec << std::endl;

            const bool ssec = cmdq_ssec(word0);
            const uint8_t command_security_state =
                secure_cmdq_command_security_state(opcode, word0);
            record_cmdq_decoded(opcode, command_security_state, ssec);
            bool command_ok = true;
            switch (opcode) {
            case ARCH_CMD_SYNC:
            {
                if (m_arch_atc_inv_sync_error_pending) {
                    m_arch_atc_inv_sync_pending_count = 0;
                    m_arch_atc_inv_sync_error_pending = false;
                    m_arch_atc_inv_sync_errors++;
                    set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ATC_INV_SYNC,
                                           "secure-atc-inv-sync");
                    command_ok = false;
                    break;
                }
                const uint32_t sync_cs =
                    static_cast<uint32_t>((word0 >> ARCH_CMD_SYNC_CS_SHIFT) &
                                          ARCH_CMD_SYNC_CS_MASK);
                m_arch_last_cmd_sync_signal = sync_cs;
                if (sync_cs == ARCH_CMD_SYNC_CS_RESERVED) {
                    set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                           "secure-cmd-sync-reserved-cs");
                    command_ok = false;
                    break;
                }
                if (sync_cs == ARCH_CMD_SYNC_CS_IRQ) {
                    emit_secure_cmdq_sync_msi(
                        word1,
                        static_cast<uint32_t>(word0 >> ARCH_CMD_SYNC_MSIDATA_SHIFT));
                }
                m_arch_cmd_syncs++;
                set_arch_irq_status_for_security_state(ARCH_IRQ_CMDQ_SYNC,
                                                       ARCH_SECURITY_SECURE);
                break;
            }
            case ARCH_CMD_PRI_RESP:
            {
                const uint8_t pri_response =
                    static_cast<uint8_t>((word1 >> 16) & 0xfu);
                if (!arch_pri_supported()) {
                    set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                           "secure-pri-resp-unsupported");
                    command_ok = false;
                    break;
                }
                if (!arch_pri_response_valid(pri_response)) {
                    set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                           "secure-pri-resp-reserved-response");
                    command_ok = false;
                    break;
                }
                complete_prg(arch_prg_index(word1),
                             pri_response,
                             cmdq_stream_id(word0) != 0,
                             cmdq_stream_id(word0),
                             cmdq_ssid_valid(word0),
                             cmdq_ssid(word0));
                break;
            }
            case ARCH_CMD_RESUME:
                complete_stall(cmdq_stream_id(word0),
                               static_cast<uint16_t>(word1 & ARCH_CMD_RESUME_STAG_MASK),
                               static_cast<uint8_t>((word0 >> ARCH_CMD_RESUME_RESP_SHIFT) &
                                                    ARCH_CMD_RESUME_RESP_MASK));
                break;
            case ARCH_CMD_STALL_TERM:
                terminate_stalls_for_stream(cmdq_stream_id(word0));
                break;
            case ARCH_CMD_CFGI_STE:
            case ARCH_CMD_CFGI_CD:
            case ARCH_CMD_CFGI_CD_ALL:
                handle_cmdq_cfgi(word0, false, command_security_state);
                break;
            case ARCH_CMD_CFGI_ALL:
                handle_cmdq_cfgi(word0, true, command_security_state);
                break;
            case ARCH_CMD_CFGI_VMS_PIDM:
                handle_cmdq_cfgi_vms_pidm(word0, command_security_state);
                break;
            case ARCH_CMD_TLBI_NH_ALL:
            case ARCH_CMD_TLBI_NH_ASID:
            case ARCH_CMD_TLBI_NH_VA:
            case ARCH_CMD_TLBI_NH_VAA:
            case ARCH_CMD_TLBI_EL3_ALL:
            case ARCH_CMD_TLBI_EL3_VA:
            case ARCH_CMD_TLBI_EL2_ALL:
            case ARCH_CMD_TLBI_EL2_ASID:
            case ARCH_CMD_TLBI_EL2_VA:
            case ARCH_CMD_TLBI_EL2_VAA:
            case ARCH_CMD_TLBI_S12_VMALL:
            case ARCH_CMD_TLBI_S2_IPA:
            case ARCH_CMD_TLBI_NSNH_ALL:
            case ARCH_CMD_TLBI_S_EL2_ALL:
            case ARCH_CMD_TLBI_S_EL2_ASID:
            case ARCH_CMD_TLBI_S_EL2_VA:
            case ARCH_CMD_TLBI_S_EL2_VAA:
            case ARCH_CMD_TLBI_S_S12_VMALL:
            case ARCH_CMD_TLBI_S_S2_IPA:
            case ARCH_CMD_TLBI_SNH_ALL:
                if (cmdq_tlbi_range_encoding_reserved(word0, word1)) {
                    set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                           "secure-tlbi-range-reserved");
                    command_ok = false;
                    break;
                }
                handle_cmdq_tlbi(opcode, word0, word1,
                                  command_security_state);
                break;
            case ARCH_CMD_ATC_INV:
                if (!arch_ats_supported()) {
                    set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                           "secure-atc-inv-unsupported");
                    command_ok = false;
                    break;
                }
                handle_cmdq_atc_inv(word0, word1, command_security_state);
                break;
            case ARCH_CMD_DPTI_ALL:
            case ARCH_CMD_DPTI_PA:
                m_arch_cmd_dptis++;
                m_arch_last_cmd_iova = cmdq_page(word1);
                set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                       "secure-dpti-unsupported");
                command_ok = false;
                break;
            default:
                m_arch_fault_reason = ARCH_FAULT_TABLE_INVALID;
                set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ILL,
                                       "secure-unsupported-cmd");
                command_ok = false;
                break;
            }

            if (!command_ok) {
                break;
            }
            m_arch_secure_cmdq.cons =
                queue_advance(m_arch_secure_cmdq.cons, entries);
        }

        if (guard == 0 && m_arch_secure_cmdq.cons != m_arch_secure_cmdq.prod) {
            set_secure_cmdq_cerror(ARCH_CMDQ_CERROR_ABT, "stalled");
            log_arch_queue("S_CMDQ", "stalled", m_arch_secure_cmdq.prod,
                           m_arch_secure_cmdq.cons);
        }
    }

    static uint64_t arch_level_index(uint64_t iova, uint32_t level)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_legacy_level_index(iova,
                                                                                level);
    }

    static uint32_t arch_page_shift(uint32_t granule)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_page_shift(granule);
    }

    static uint32_t arch_index_bits(uint32_t granule)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_index_bits(granule);
    }

    static uint64_t arch_granule_size(uint32_t granule)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_granule_size(granule);
    }

    static uint64_t arch_granule_mask(uint32_t granule)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_granule_mask(granule);
    }

    static bool arch_granule_supported(uint32_t granule)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_granule_supported(granule);
    }

    static uint64_t arch_output_mask(uint32_t granule)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_output_mask(granule);
    }

    static uint32_t arch_encoded_granule(uint64_t word)
    {
        return static_cast<uint32_t>((word >> ARCH_GRANULE_SHIFT) & ARCH_GRANULE_MASK);
    }

    static uint32_t arch_encoded_start_level(uint64_t word)
    {
        return static_cast<uint32_t>((word >> ARCH_START_LEVEL_SHIFT) & ARCH_START_LEVEL_MASK);
    }

    static uint32_t arch_walk_levels(uint32_t granule)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_levels(granule);
    }

    static uint32_t arch_walk_covered_bits(const arch_walk_config& cfg)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_covered_bits(
            cfg.granule, cfg.start_level, cfg.levels);
    }

    static uint64_t arch_level_index(uint64_t iova, const arch_walk_config& cfg, uint32_t level)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_level_index(
            iova, cfg.granule, cfg.levels, level);
    }

    static uint64_t arch_level_offset_mask(const arch_walk_config& cfg, uint32_t level)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::walker_level_offset_mask(
            cfg.granule, cfg.levels, level);
    }

    static uint32_t arch_ste_config(uint64_t ste0)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_config(ste0);
    }

    static bool arch_ste_has_stage1(uint64_t ste0)
    {
        if ((ste0 & ARCH_STE_VALID) == 0) {
            return false;
        }

        const uint32_t cfg = arch_ste_config(ste0);
        return cfg == ARCH_STE_CFG_S1_TRANS || cfg == ARCH_STE_CFG_NESTED;
    }

    static bool arch_ste_has_stage2(uint64_t ste0)
    {
        if ((ste0 & ARCH_STE_VALID) == 0) {
            return false;
        }

        const uint32_t cfg = arch_ste_config(ste0);
        return cfg == ARCH_STE_CFG_S2_TRANS || cfg == ARCH_STE_CFG_NESTED;
    }

    bool arch_atos_validate_ste_config(uint32_t stream_id, uint64_t ste0,
                                       uint64_t ste_pa)
    {
        if (!m_arch_atos_request_active || (ste0 & ARCH_STE_VALID) == 0) {
            return true;
        }

        bool valid = false;
        switch (m_arch_atos_type) {
        case ARCH_ATOS_ADDR_TYPE_STAGE1:
            valid = arch_ste_has_stage1(ste0);
            break;
        case ARCH_ATOS_ADDR_TYPE_STAGE2:
            valid = arch_ste_has_stage2(ste0);
            break;
        case ARCH_ATOS_ADDR_TYPE_STAGE1_STAGE2:
            valid = arch_ste_config(ste0) == ARCH_STE_CFG_NESTED;
            break;
        default:
            m_arch_fault_reason = ARCH_FAULT_ATOS_INV_REQ;
            m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
            return false;
        }

        if (valid) {
            return true;
        }

        m_arch_fault_reason = ARCH_FAULT_ATOS_INV_STAGE;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: ATOS_ADDR.TYPE invalid stage"
                     << " stream-id=0x" << std::hex << stream_id
                     << " ste-pa=0x" << ste_pa << " ste=0x" << ste0
                     << " type=0x" << m_arch_atos_type << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: ATOS_ADDR.TYPE invalid stage"
                  << " stream-id=0x" << std::hex << stream_id
                  << " ste-pa=0x" << ste_pa << " ste=0x" << ste0
                  << " type=0x" << m_arch_atos_type << std::dec << std::endl;
        return false;
    }

    static bool arch_ste_config_supported(uint64_t ste0)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_config_supported(ste0);
    }

    static bool arch_has_reserved_bits(uint64_t word, uint64_t modeled_mask)
    {
        return (word & ~modeled_mask) != 0;
    }

    bool arch_reject_reserved_ste(uint32_t stream_id, uint64_t ste_pa, uint64_t ste0,
                                  uint64_t ste1, uint64_t ste2)
    {
        if ((ste0 & ARCH_STE_VALID) != 0 && !arch_ste_config_supported(ste0)) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: illegal STE.Config encoding"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ste-pa=0x" << ste_pa << " ste=0x" << ste0 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: illegal STE.Config encoding"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ste-pa=0x" << ste_pa << " ste=0x" << ste0 << std::dec
                      << std::endl;
            return true;
        }

        if (arch_ste_stage2_enabled(ste0) &&
            arch_stall_model_terminates_stage2_stalls() &&
            arch_ste_s2_stall(ste1)) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: illegal STE.S2S for terminate-only STALL_MODEL"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ste-pa=0x" << ste_pa << " ste0=0x" << ste0
                         << " ste1=0x" << ste1 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: illegal STE.S2S for terminate-only STALL_MODEL"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ste-pa=0x" << ste_pa << " ste0=0x" << ste0
                      << " ste1=0x" << ste1 << std::dec << std::endl;
            return true;
        }

        if (arch_ste_stage2_enabled(ste0) &&
            arch_ste_s2haft(ste2) && !arch_ste_s2ha(ste2)) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: illegal STE.S2HAFT without S2HA"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ste-pa=0x" << ste_pa << " ste0=0x" << ste0
                         << " ste2=0x" << ste2 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: illegal STE.S2HAFT without S2HA"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ste-pa=0x" << ste_pa << " ste0=0x" << ste0
                      << " ste2=0x" << ste2 << std::dec << std::endl;
            return true;
        }

        if (arch_has_reserved_bits(ste0, ARCH_STE0_MODELED_MASK) ||
            arch_has_reserved_bits(ste1, ARCH_STE1_MODELED_MASK) ||
            arch_has_reserved_bits(ste2, ARCH_STE2_MODELED_MASK)) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: reserved STE encoding"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ste-pa=0x" << ste_pa << " ste0=0x" << ste0
                         << " ste1=0x" << ste1 << " ste2=0x" << ste2 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: reserved STE encoding"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ste-pa=0x" << ste_pa << " ste0=0x" << ste0
                      << " ste1=0x" << ste1 << " ste2=0x" << ste2 << std::dec
                      << std::endl;
            return true;
        }

        return false;
    }

    bool arch_reject_reserved_cd(uint32_t stream_id, uint64_t cd_pa, uint64_t cd0,
                                 uint64_t cd1, uint64_t cd2)
    {
        if (arch_has_reserved_bits(cd0, ARCH_CD0_MODELED_MASK) ||
            arch_has_reserved_bits(cd1, ARCH_CD1_MODELED_MASK) ||
            arch_has_reserved_bits(cd2, ARCH_CD2_MODELED_MASK)) {
            m_arch_fault_reason = ARCH_FAULT_CD_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: reserved CD encoding"
                         << " stream-id=0x" << std::hex << stream_id
                         << " cd-pa=0x" << cd_pa << " cd0=0x" << cd0
                         << " cd1=0x" << cd1 << " cd2=0x" << cd2 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: reserved CD encoding"
                      << " stream-id=0x" << std::hex << stream_id
                      << " cd-pa=0x" << cd_pa << " cd0=0x" << cd0
                      << " cd1=0x" << cd1 << " cd2=0x" << cd2 << std::dec
                      << std::endl;
            return true;
        }

        if (arch_cd_haft(cd1) && !arch_cd_ha(cd0)) {
            m_arch_fault_reason = ARCH_FAULT_CD_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: illegal CD.HAFT without HA"
                         << " stream-id=0x" << std::hex << stream_id
                         << " cd-pa=0x" << cd_pa << " cd0=0x" << cd0
                         << " cd1=0x" << cd1 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: illegal CD.HAFT without HA"
                      << " stream-id=0x" << std::hex << stream_id
                      << " cd-pa=0x" << cd_pa << " cd0=0x" << cd0
                      << " cd1=0x" << cd1 << std::dec << std::endl;
            return true;
        }

        return false;
    }

    static arch_walk_config arch_walk_config_from_word(uint64_t word, uint32_t stage)
    {
        arch_walk_config cfg {};

        cfg.granule = arch_encoded_granule(word);
        cfg.start_level = arch_encoded_start_level(word);
        cfg.levels = arch_walk_levels(cfg.granule);
        cfg.stage = stage;
        return cfg;
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
        case ARCH_FAULT_BAD_STREAM_ID:
            return "bad-stream-id";
        case ARCH_FAULT_UNSUPPORTED_UPSTREAM:
            return "unsupported-upstream";
        case ARCH_FAULT_ATOS_INV_STAGE:
            return "atos-invalid-stage";
        case ARCH_FAULT_ATOS_INV_REQ:
            return "atos-invalid-request";
        case ARCH_FAULT_ACCESS:
            return "access-flag";
        case ARCH_FAULT_PERMISSION:
            return "permission";
        case ARCH_FAULT_ADDR_SIZE:
            return "address-size";
        case ARCH_FAULT_GRANULE:
            return "unsupported-granule";
        case ARCH_FAULT_STAGE2:
            return "stage2";
        case ARCH_FAULT_BAD_ATS_TREQ:
            return "bad-ats-translation-request";
        case ARCH_FAULT_TRANSL_FORBIDDEN:
            return "translation-forbidden";
        case ARCH_FAULT_TLB_CONFLICT:
            return "tlb-conflict";
        case ARCH_FAULT_CFG_CONFLICT:
            return "cfg-conflict";
        case ARCH_FAULT_WALK_EABT:
            return "walk-external-abort";
        case ARCH_FAULT_VMS_FETCH:
            return "vms-fetch-external-abort";
        case ARCH_FAULT_STREAM_DISABLED:
            return "stream-disabled";
        case ARCH_FAULT_BAD_SUBSTREAMID:
            return "bad-substream-id";
        default:
            return "unknown";
        }
    }

    uint32_t arch_protocol_status() const
    {
        return m_arch_core.protocol_status();
    }

    uint32_t arch_ats_detail() const
    {
        return m_arch_core.ats_detail();
    }

    uint32_t arch_pri_pending_count() const
    {
        uint32_t count = 0;

        for (const auto& request : m_arch_pri_pending) {
            if (request.pending) {
                count++;
            }
        }
        return count;
    }

    uint32_t arch_pri_detail() const
    {
        return m_arch_core.pri_detail(arch_pri_pending_count());
    }

    uint32_t arch_stall_status() const
    {
        return m_arch_core.stall_status();
    }

    uint32_t arch_stall_merge_status() const
    {
        return m_arch_core.stall_merge_status();
    }

    uint32_t arch_endpoint_replay_status() const
    {
        return m_arch_core.endpoint_replay_status();
    }

    uint32_t arch_endpoint_replay_ctrl() const
    {
        return m_arch_core.endpoint_replay_ctrl();
    }

    uint32_t arch_endpoint_block_status() const
    {
        return m_arch_core.endpoint_block_status();
    }

    uint32_t arch_early_retry_status() const
    {
        return m_arch_core.early_retry_status();
    }

    bool redrive_endpoint_replay(arch_endpoint_replay_record& replay)
    {
        if (!m_arch_core.begin_endpoint_replay_redrive(replay)) {
            return false;
        }

        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        uint64_t offset = 0;

        while (offset < replay.len) {
            uint64_t pa = 0;
            uint64_t segment_len = 0;

            if (!translate_segment(replay.stream_id, replay.iova + offset,
                                   replay.len - offset, pa, segment_len,
                                   replay.ssid_valid, replay.ssid) ||
                segment_len == 0) {
                m_arch_core.fail_endpoint_replay_redrive(
                    replay, tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return false;
            }

            const auto segment =
                m_arch_core.prepare_endpoint_replay_segment(replay, offset, pa,
                                                            segment_len);
            if (!segment.valid) {
                return false;
            }

            const auto transaction =
                m_arch_core.begin_endpoint_replay_transaction(replay, segment);
            if (!transaction.valid) {
                return false;
            }

            const auto status =
                execute_endpoint_replay_transaction(transaction, delay);
            if (!m_arch_core.complete_endpoint_replay_transaction(
                    replay, segment, transaction, status)) {
                return false;
            }

            offset += segment_len;
        }

        return m_arch_core.finish_endpoint_replay_redrive(replay);
    }

    bool discard_uncommitted_early_retry(arch_endpoint_replay_record& replay)
    {
        auto* const stall = find_stall(replay.stream_id, replay.stag);

        if (stall == nullptr || stall->event_committed ||
            !discard_buffered_stall_event(replay.stream_id, replay.stag)) {
            return false;
        }

        stall->pending = false;
        replay.pending = false;
        replay.replayed = true;
        m_arch_stall_pending =
            m_arch_stall_pending == 0 ? 0 : m_arch_stall_pending - 1;
        m_arch_endpoint_replay_pending =
            m_arch_endpoint_replay_pending == 0 ? 0 : m_arch_endpoint_replay_pending - 1;
        m_arch_early_retry_discarded++;
        m_arch_endpoint_replay_resume_event.notify(sc_core::SC_ZERO_TIME);
        SCP_INFO(()) << "APOLLO_SMMU_TBU: endpoint early retry discarded stale event"
                     << " stream-id=0x" << std::hex << replay.stream_id
                     << " stag=0x" << replay.stag
                     << " iova=0x" << replay.iova << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: endpoint early retry discarded stale event"
                  << " stream-id=0x" << std::hex << replay.stream_id
                  << " stag=0x" << replay.stag
                  << " iova=0x" << replay.iova << std::dec << std::endl;
        return true;
    }

    uint32_t early_retry_endpoint_replays()
    {
        uint32_t attempted = 0;

        for (auto& replay : m_arch_endpoint_replays) {
            if (!replay.pending || replay.early_retry_succeeded) {
                continue;
            }

            attempted++;
            m_arch_early_retry_attempted++;
            if (redrive_endpoint_replay(replay)) {
                replay.succeeded = true;
                replay.early_retry_succeeded = true;
                discard_uncommitted_early_retry(replay);
                m_arch_early_retry_succeeded++;
                SCP_INFO(()) << "APOLLO_SMMU_TBU: endpoint early retry succeeded"
                             << " stream-id=0x" << std::hex << replay.stream_id
                             << " stag=0x" << replay.stag
                             << " iova=0x" << replay.iova
                             << " pa=0x" << replay.replay_pa
                             << " len=0x" << replay.replay_len << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: endpoint early retry succeeded"
                          << " stream-id=0x" << std::hex << replay.stream_id
                          << " stag=0x" << replay.stag
                          << " iova=0x" << replay.iova
                          << " pa=0x" << replay.replay_pa
                          << " len=0x" << replay.replay_len << std::dec << std::endl;
            } else {
                m_arch_early_retry_failed++;
                SCP_WARN(()) << "APOLLO_SMMU_TBU: endpoint early retry still stalled"
                             << " stream-id=0x" << std::hex << replay.stream_id
                             << " stag=0x" << replay.stag
                             << " iova=0x" << replay.iova
                             << " status=" << replay.replay_status << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: endpoint early retry still stalled"
                          << " stream-id=0x" << std::hex << replay.stream_id
                          << " stag=0x" << replay.stag
                          << " iova=0x" << replay.iova
                          << " status=" << replay.replay_status << std::dec << std::endl;
            }
        }

        return attempted;
    }

    uint32_t arch_cmd_status() const
    {
        return (m_arch_cmd_processed & 0xffu) |
               ((m_arch_cmd_invalidations & 0xffu) << 8) |
               ((m_arch_cmd_syncs & 0xffu) << 16) |
               ((m_arch_last_cmd_opcode & 0xffu) << 24);
    }

    uint32_t arch_cmd_detail() const
    {
        return (m_arch_last_cmd_invalidated & 0xffu) |
               ((m_arch_last_cmd_stream_id & 0xffffu) << 8) |
               ((m_arch_cmd_cfgis & 0x0fu) << 24) |
               ((m_arch_cmd_tlbis & 0x0fu) << 28);
    }

    static const char* arch_ats_response_name(uint8_t status)
    {
        switch (status) {
        case ARCH_ATS_RESP_SUCCESS:
            return "success";
        case ARCH_ATS_RESP_UR:
            return "unsupported-request";
        case ARCH_ATS_RESP_CA:
            return "completer-abort";
        default:
            return "unknown";
        }
    }

    uint8_t arch_ats_response_code(bool success) const
    {
        if (success) {
            return ARCH_ATS_RESP_SUCCESS;
        }

        switch (m_arch_fault_reason) {
        case ARCH_FAULT_STE_FETCH:
        case ARCH_FAULT_STE_INVALID:
        case ARCH_FAULT_CD_FETCH:
        case ARCH_FAULT_CD_INVALID:
        case ARCH_FAULT_BAD_STREAM_ID:
        case ARCH_FAULT_BAD_ATS_TREQ:
        case ARCH_FAULT_STREAM_DISABLED:
            return ARCH_ATS_RESP_UR;
        default:
            return ARCH_ATS_RESP_CA;
        }
    }

    static bool arch_ats_treq_translation_fault_has_no_smmu_event(
        uint32_t fault_reason)
    {
        /*
         * SMMUv3 §3.9.1.2: ATS Translation Requests that reach the translation
         * process and hit Address Size, Access, Translation, or selected
         * Permission faults complete successfully with R==W==0 and do not
         * record an SMMU fault.  Configuration/fetch/protocol errors still use
         * the normal UR/CA and REC_CFG_ATS recording paths.
         */
        switch (fault_reason) {
        case ARCH_FAULT_ADDR_SIZE:
        case ARCH_FAULT_ACCESS:
        case ARCH_FAULT_PERMISSION:
        case ARCH_FAULT_TABLE_INVALID:
        case ARCH_FAULT_PAGE_INVALID:
            return true;
        default:
            return false;
        }
    }

    void record_arch_ats_response(uint8_t status)
    {
        m_arch_ats_responses++;
        switch (status) {
        case ARCH_ATS_RESP_SUCCESS:
            m_arch_ats_success++;
            break;
        case ARCH_ATS_RESP_UR:
            m_arch_ats_ur++;
            break;
        case ARCH_ATS_RESP_CA:
            m_arch_ats_ca++;
            break;
        default:
            break;
        }
    }

    uint8_t arch_ats_treq_response_code(bool success) const
    {
        if (success) {
            return ARCH_ATS_RESP_SUCCESS;
        }

        switch (m_arch_fault_reason) {
        case ARCH_FAULT_BAD_ATS_TREQ:
            return ARCH_ATS_RESP_UR;
        case ARCH_FAULT_STREAM_DISABLED:
            /*
             * STE.Config==0 ATS Translation Requests are the architected
             * disabled-stream UR/no-event case.  F_STREAM_DISABLED faults that
             * arise during S1DSS/CD configuration lookup remain configuration
             * errors and complete with CA.
             */
            return m_arch_fault_record_suppressed ? ARCH_ATS_RESP_UR :
                                                    ARCH_ATS_RESP_CA;
        default:
            return ARCH_ATS_RESP_CA;
        }
    }

    bool pri_prg_pending(uint16_t prg) const
    {
        const uint16_t prg_index = arch_prg_index(prg);
        if (prg_index == 0) {
            return false;
        }

        for (const auto& request : m_arch_pri_pending) {
            if (request.pending && request.prg == prg_index) {
                return true;
            }
        }
        return false;
    }

    uint16_t allocate_prg(uint32_t stream_id, uint64_t iova, uint8_t ats_status,
                          bool ssid_valid = false, uint32_t ssid = 0)
    {
        for (auto& request : m_arch_pri_pending) {
            if (!request.pending) {
                uint16_t prg = 0;
                for (uint32_t attempts = 0; attempts <= ARCH_PRIQ_PPR_PRG_MASK; ++attempts) {
                    prg = arch_prg_index(m_arch_next_prg++);
                    if (prg != 0 && !pri_prg_pending(prg)) {
                        break;
                    }
                    prg = 0;
                }
                if (prg == 0) {
                    break;
                }

                request.stream_id = stream_id;
                request.prg = prg;
                request.ats_status = ats_status;
                request.security_state = m_arch_last_security_state;
                request.iova = iova;
                request.ssid_valid = ssid_valid;
                request.ssid = ssid_valid ? (ssid & ARCH_CMDQ_SSID_MASK) : 0;
                request.pending = true;
                m_arch_last_prg = request.prg;
                m_arch_pri_accepted++;
                return request.prg;
            }
        }

        m_arch_pri_unknown++;
        set_arch_gerror(ARCH_GERROR_QUEUE_OVERFLOW);
        return 0;
    }

    bool clear_pending_prg(uint16_t prg)
    {
        const uint16_t prg_index = arch_prg_index(prg);
        if (prg_index == 0) {
            return false;
        }
        for (auto& request : m_arch_pri_pending) {
            if (request.pending && request.prg == prg_index) {
                request.pending = false;
                return true;
            }
        }
        return false;
    }

    void record_pri_auto_response(uint32_t stream_id, uint16_t prg, uint8_t response,
                                  const char* reason, bool response_ssv = false,
                                  uint32_t response_ssid = 0)
    {
        m_arch_pri_auto_responses++;
        if (response == ARCH_PRI_RESP_FAILURE) {
            m_arch_pri_auto_failures++;
        }
        m_arch_last_prg = prg;
        m_arch_last_auto_prg = prg;
        m_arch_last_auto_response = response;
        m_arch_last_auto_response_ssv = response_ssv;
        m_arch_last_auto_response_ssid =
            response_ssv ? (response_ssid & ARCH_PRIQ_PPR_SSID_MASK) : 0;
        clear_pending_prg(prg);
        SCP_WARN(()) << "APOLLO_SMMU_TBU: architected PRI auto-response"
                     << " stream-id=0x" << std::hex << stream_id
                     << " prg=0x" << prg
                     << " response=0x" << static_cast<uint32_t>(response)
                     << " ssv=" << response_ssv
                     << " ssid=0x" << m_arch_last_auto_response_ssid
                     << " reason=" << reason << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected PRI auto-response"
                  << " stream-id=0x" << std::hex << stream_id
                  << " prg=0x" << prg
                  << " response=0x" << static_cast<uint32_t>(response)
                  << " ssv=" << response_ssv
                  << " ssid=0x" << m_arch_last_auto_response_ssid
                  << " reason=" << reason << std::dec << std::endl;
    }

    bool arch_priq_abort_active(uint8_t security_state = ARCH_SECURITY_NONSECURE) const
    {
        if (arch_security_uses_secure_irq_bank(security_state)) {
            return (read_secure_gerror() & ARCH_GERROR_PRIQ_ABORT) != 0;
        }

        return (read_arch_gerror() & ARCH_GERROR_PRIQ_ABORT) != 0;
    }

    static bool arch_pri_response_valid(uint8_t response)
    {
        return response == ARCH_PRI_RESP_ACCEPT ||
               response == ARCH_PRI_RESP_REJECT ||
               response == ARCH_PRI_RESP_FAILURE;
    }

    static uint16_t arch_prg_index(uint64_t value)
    {
        return static_cast<uint16_t>(value & ARCH_PRIQ_PPR_PRG_MASK);
    }

    void advance_priq_cons_after_response(const arch_pri_request& request)
    {
        auto& priq = arch_priq_for_security_state(request.security_state);
        const uint32_t entries = queue_entries(priq);

        if (entries == 0 || queue_base_addr(priq) == 0 ||
            priq.cons == priq.prod) {
            return;
        }

        priq.cons = queue_advance(priq.cons, entries);
        if (priq.cons == priq.prod) {
            clear_arch_irq_status(ARCH_IRQ_PRIQ);
        }
    }

    bool complete_prg(uint16_t prg, uint8_t response,
                      bool stream_id_valid = false,
                      uint32_t response_stream_id = 0,
                      bool ssid_valid = false,
                      uint32_t response_ssid = 0)
    {
        /*
         * Architected CMD_PRI_RESP retires the oldest pending Page Request
         * Group entry only.  A response that names a later PRGIndex is a
         * command/driver ordering error for this functional model: preserve the
         * pending queue and expose a diagnostic instead of clearing the later
         * entry out of order.
         */
        arch_pri_request* head = nullptr;
        bool prg_seen_behind_head = false;
        arch_pri_request* selected = nullptr;
        bool prg_seen_with_stream_mismatch = false;
        bool prg_seen_with_ssid_mismatch = false;
        const uint16_t prg_index = arch_prg_index(prg);
        const uint32_t command_ssid = response_ssid & ARCH_CMDQ_SSID_MASK;

        for (auto& request : m_arch_pri_pending) {
            if (!request.pending) {
                continue;
            }
            if (head == nullptr) {
                head = &request;
            }
            if (request.prg == prg_index && &request != head) {
                prg_seen_behind_head = true;
            }
        }

        if (head != nullptr) {
            if (prg == 0 || head->prg == prg_index) {
                if (stream_id_valid && head->stream_id != response_stream_id) {
                    prg_seen_with_stream_mismatch = true;
                } else if (ssid_valid &&
                           (!head->ssid_valid || head->ssid != command_ssid)) {
                    prg_seen_with_ssid_mismatch = true;
                } else {
                    selected = head;
                }
            }
        }

        m_arch_pri_responses++;
        if (selected == nullptr) {
            m_arch_pri_unknown++;
            m_arch_fault_replay.last_pri_response_valid = false;
            m_arch_fault_replay.last_pri_response_unknown = true;
            m_arch_fault_replay.last_pri_response_order_mismatch =
                head != nullptr && prg != 0 && head->prg != prg_index;
            m_arch_fault_replay.last_pri_response_head_prg =
                head != nullptr ? head->prg : 0;
            m_arch_fault_replay.last_pri_response_stream_mismatch =
                prg_seen_with_stream_mismatch;
            m_arch_fault_replay.last_pri_response_cmd_stream_id =
                stream_id_valid ? response_stream_id : 0;
            m_arch_fault_replay.last_pri_response_ssid_mismatch =
                prg_seen_with_ssid_mismatch;
            m_arch_fault_replay.last_pri_response_cmd_ssid_valid =
                ssid_valid;
            m_arch_fault_replay.last_pri_response_cmd_ssid =
                ssid_valid ? command_ssid : 0;
            m_arch_fault_replay.last_pri_response_ssid_valid = false;
            m_arch_fault_replay.last_pri_response_ssid = 0;
            m_arch_fault_replay.last_pri_response_stream_id = 0;
            m_arch_fault_replay.last_pri_response_code = response;
            m_arch_fault_replay.last_pri_response_ats_status = 0;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: architected PRI response unknown-prg=0x"
                         << std::hex << prg_index << " response=0x"
                         << static_cast<uint32_t>(response)
                         << " head-prg=0x"
                         << (head != nullptr ? head->prg : 0)
                         << " order-mismatch="
                         << m_arch_fault_replay.last_pri_response_order_mismatch
                         << " prg-behind-head=" << prg_seen_behind_head
                         << " cmd-stream-id=0x"
                         << (stream_id_valid ? response_stream_id : 0)
                         << " stream-mismatch=" << prg_seen_with_stream_mismatch
                         << " cmd-ssid=0x" << (ssid_valid ? command_ssid : 0)
                         << " ssid-mismatch=" << prg_seen_with_ssid_mismatch
                         << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected PRI response unknown-prg=0x"
                      << std::hex << prg_index << " response=0x"
                      << static_cast<uint32_t>(response)
                      << " head-prg=0x"
                      << (head != nullptr ? head->prg : 0)
                      << " order-mismatch="
                      << m_arch_fault_replay.last_pri_response_order_mismatch
                      << " prg-behind-head=" << prg_seen_behind_head
                      << " cmd-stream-id=0x"
                      << (stream_id_valid ? response_stream_id : 0)
                      << " stream-mismatch=" << prg_seen_with_stream_mismatch
                      << " cmd-ssid=0x" << (ssid_valid ? command_ssid : 0)
                      << " ssid-mismatch=" << prg_seen_with_ssid_mismatch
                      << std::dec << std::endl;
            return false;
        }

        advance_priq_cons_after_response(*selected);
        selected->pending = false;
        m_arch_last_prg = selected->prg;
        m_arch_fault_replay.last_pri_response_valid = true;
        m_arch_fault_replay.last_pri_response_unknown = false;
        m_arch_fault_replay.last_pri_response_order_mismatch = false;
        m_arch_fault_replay.last_pri_response_head_prg = selected->prg;
        m_arch_fault_replay.last_pri_response_stream_mismatch = false;
        m_arch_fault_replay.last_pri_response_ssid_mismatch = false;
        m_arch_fault_replay.last_pri_response_cmd_stream_id =
            stream_id_valid ? response_stream_id : selected->stream_id;
        m_arch_fault_replay.last_pri_response_cmd_ssid_valid = ssid_valid;
        m_arch_fault_replay.last_pri_response_cmd_ssid =
            ssid_valid ? command_ssid : 0;
        m_arch_fault_replay.last_pri_response_stream_id = selected->stream_id;
        m_arch_fault_replay.last_pri_response_ssid_valid =
            selected->ssid_valid;
        m_arch_fault_replay.last_pri_response_ssid =
            selected->ssid_valid ? selected->ssid : 0;
        m_arch_fault_replay.last_pri_response_code = response;
        m_arch_fault_replay.last_pri_response_ats_status = selected->ats_status;
        if (response != ARCH_PRI_RESP_ACCEPT) {
            m_arch_pri_rejected++;
        }
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected PRI response clear stream-id=0x"
                     << std::hex << selected->stream_id << " prg=0x" << selected->prg
                     << " response=0x" << static_cast<uint32_t>(response)
                     << " ssid-valid=" << selected->ssid_valid
                     << " ssid=0x" << selected->ssid
                     << " ats-status=" << arch_ats_response_name(selected->ats_status) << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected PRI response clear stream-id=0x"
                  << std::hex << selected->stream_id << " prg=0x" << selected->prg
                  << " response=0x" << static_cast<uint32_t>(response)
                  << " ssid-valid=" << selected->ssid_valid
                  << " ssid=0x" << selected->ssid
                  << " ats-status=" << arch_ats_response_name(selected->ats_status) << std::dec
                  << std::endl;
        return true;
    }

    bool complete_endpoint_replay(uint32_t stream_id, uint16_t stag, uint8_t response)
    {
        arch_endpoint_replay_record* replay = find_endpoint_replay(stream_id, stag);

        if (replay == nullptr) {
            return false;
        }

        if (response == ARCH_CMD_RESUME_RESP_RETRY) {
            bool redrive_succeeded = true;
            if (replay->early_retry_succeeded) {
                redrive_succeeded = true;
            } else {
                redrive_succeeded = redrive_endpoint_replay(*replay);
            }
            m_arch_core.retire_endpoint_replay(*replay, true, redrive_succeeded);
            SCP_INFO(()) << "APOLLO_SMMU_TBU: endpoint transaction replay"
                         << " stream-id=0x" << std::hex << stream_id
                         << " stag=0x" << stag << " iova=0x" << replay->iova
                         << " pa=0x" << replay->replay_pa
                         << " len=0x" << replay->replay_len
                         << " redriven=" << replay->redriven
                         << " success=" << replay->succeeded << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: endpoint transaction replay"
                      << " stream-id=0x" << std::hex << stream_id
                      << " stag=0x" << stag << " iova=0x" << replay->iova
                      << " pa=0x" << replay->replay_pa
                      << " len=0x" << replay->replay_len
                      << " redriven=" << replay->redriven
                      << " success=" << replay->succeeded << std::dec << std::endl;
        } else {
            m_arch_core.retire_endpoint_replay(*replay, false, false);
        }
        m_arch_endpoint_replay_resume_event.notify(sc_core::SC_ZERO_TIME);
        return true;
    }

    tlm::tlm_response_status wait_endpoint_replay_resume(uint32_t stream_id, uint16_t stag,
                                                          bool read, uint8_t* data,
                                                          size_t len)
    {
        arch_endpoint_replay_record* replay = find_endpoint_replay_any(stream_id, stag);

        if (replay == nullptr) {
            m_arch_endpoint_block_failed++;
            return tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }

        m_arch_endpoint_block_waits++;
        while (replay->pending && !replay->replayed) {
            sc_core::wait(m_arch_endpoint_replay_resume_event);
        }

        if (replay->succeeded && replay->replay_status == tlm::TLM_OK_RESPONSE) {
            if (read && data != nullptr && !replay->payload.empty()) {
                const size_t copy_len = std::min(len, replay->payload.size());
                std::memcpy(data, replay->payload.data(), copy_len);
            }
            m_arch_endpoint_block_resumed++;
            return tlm::TLM_OK_RESPONSE;
        }

        m_arch_endpoint_block_failed++;
        if (replay->replay_status != tlm::TLM_INCOMPLETE_RESPONSE) {
            return replay->replay_status;
        }
        return tlm::TLM_ADDRESS_ERROR_RESPONSE;
    }

    bool complete_stall(uint32_t stream_id, uint16_t stag, uint8_t response)
    {
        arch_stall_record* stall = find_stall(stream_id, stag);

        m_arch_last_resume_stag = stag;
        if (stall == nullptr) {
            m_arch_resume_unknown++;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: architected RESUME without matching STAG"
                         << " stream-id=0x" << std::hex << stream_id
                         << " stag=0x" << stag
                         << " response=0x" << static_cast<uint32_t>(response) << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected RESUME without matching STAG"
                      << " stream-id=0x" << std::hex << stream_id
                      << " stag=0x" << stag
                      << " response=0x" << static_cast<uint32_t>(response)
                      << std::dec << std::endl;
            return false;
        }

        complete_endpoint_replay(stream_id, stag, response);
        stall->pending = false;
        m_arch_stall_pending--;
        if (response == ARCH_CMD_RESUME_RESP_RETRY) {
            m_arch_stall_retried++;
        } else {
            m_arch_stall_terminated++;
            m_arch_fault_replays++;
        }

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected RESUME handled response=0x"
                     << std::hex << static_cast<uint32_t>(response)
                     << " stream-id=0x" << stream_id
                     << " stag=0x" << stag
                     << " pending=" << std::dec << m_arch_stall_pending
                     << " retry=" << m_arch_stall_retried
                     << " terminate=" << m_arch_stall_terminated;
        std::cerr << "APOLLO_SMMU_TBU: architected RESUME handled response=0x"
                  << std::hex << static_cast<uint32_t>(response)
                  << " stream-id=0x" << stream_id
                  << " stag=0x" << stag
                  << " pending=" << std::dec << m_arch_stall_pending
                  << " retry=" << m_arch_stall_retried
                  << " terminate=" << m_arch_stall_terminated << std::endl;
        return true;
    }

    bool terminate_stalls_for_stream(uint32_t stream_id)
    {
        uint32_t terminated = 0;

        for (auto& stall : m_arch_stalls) {
            if (stall.pending && stall.stream_id == stream_id) {
                complete_endpoint_replay(stream_id, stall.stag, ARCH_CMD_RESUME_RESP_TERM);
                stall.pending = false;
                terminated++;
            }
        }

        if (terminated == 0) {
            m_arch_resume_unknown++;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: architected STALL_TERM without pending stall"
                         << " stream-id=0x" << std::hex << stream_id << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected STALL_TERM without pending stall"
                      << " stream-id=0x" << std::hex << stream_id << std::dec << std::endl;
            return false;
        }

        m_arch_stall_pending -= std::min(m_arch_stall_pending, terminated);
        m_arch_stall_terminated += terminated;
        m_arch_fault_replays += terminated;

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected STALL_TERM handled"
                     << " stream-id=0x" << std::hex << stream_id
                     << " terminated=" << std::dec << terminated
                     << " pending=" << m_arch_stall_pending
                     << " terminate=" << m_arch_stall_terminated;
        std::cerr << "APOLLO_SMMU_TBU: architected STALL_TERM handled"
                  << " stream-id=0x" << std::hex << stream_id
                  << " terminated=" << std::dec << terminated
                  << " pending=" << m_arch_stall_pending
                  << " terminate=" << m_arch_stall_terminated << std::endl;
        return true;
    }

    uint32_t arch_strtab_log2size() const
    {
        return apollo::smmuv3::apollo_smmu_arch_core::stream_table_log2size(
            m_arch_strtab_cfg);
    }

    uint32_t arch_strtab_split() const
    {
        return apollo::smmuv3::apollo_smmu_arch_core::stream_table_split(
            m_arch_strtab_cfg);
    }

    uint32_t arch_strtab_format() const
    {
        return apollo::smmuv3::apollo_smmu_arch_core::stream_table_format(
            m_arch_strtab_cfg);
    }

    bool arch_stream_id_in_bounds(uint32_t stream_id) const
    {
        return apollo::smmuv3::apollo_smmu_arch_core::stream_id_in_bounds(
            m_arch_strtab_cfg, stream_id);
    }

    static uint32_t arch_strtab_split(uint32_t cfg)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::stream_table_split(cfg);
    }

    static uint32_t arch_strtab_format(uint32_t cfg)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::stream_table_format(cfg);
    }

    static bool arch_stream_id_in_bounds(uint32_t cfg, uint32_t stream_id)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::stream_id_in_bounds(
            cfg, stream_id);
    }

    bool arch_ste_is_s1_enabled(uint64_t ste0) const
    {
        /*
         * Accept both the architected STE.Config=S1_TRANS encoding and the
         * legacy two-bit probe encoding used by the existing Apollo Linux
         * smoke test. The former is the compliance path; the latter preserves
         * the repo-local compatibility ABI until the guest probe is fully
         * converted to spec-bitfield descriptors.
         */
        return apollo::smmuv3::apollo_smmu_arch_core::ste_is_s1_enabled(ste0);
    }

    static bool arch_cd_is_valid(uint64_t cd0)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::cd_is_valid(cd0);
    }

    static uint64_t arch_cd_ttbr(uint64_t cd1)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::cd_ttbr(cd1);
    }

    static uint16_t arch_cd_asid(uint64_t cd0)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::cd_asid(cd0);
    }

    static uint32_t arch_ste_s1fmt(uint64_t ste0)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_s1fmt(ste0);
    }

    static uint32_t arch_ste_s1cdmax(uint64_t ste0)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_s1cdmax(ste0);
    }

    static uint32_t arch_ste_s1dss(uint64_t ste1)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_s1dss(ste1);
    }

    static uint32_t arch_ste_eats(uint64_t ste1)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_eats(ste1);
    }

    static bool arch_ste_s1mpam(uint64_t ste1)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_s1mpam(ste1);
    }

    static uint64_t arch_ste_vmsptr(uint64_t ste5)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_vmsptr(ste5);
    }

    static uint16_t arch_ste_partid(uint64_t ste4)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_partid(ste4);
    }

    static uint8_t arch_ste_pmg(uint64_t ste5)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::ste_pmg(ste5);
    }

    static uint16_t arch_cd_partid(uint64_t cd5)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::cd_partid(cd5);
    }

    static uint8_t arch_cd_pmg(uint64_t cd5)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::cd_pmg(cd5);
    }

    static uint16_t arch_mpam_reg_partid(uint32_t value)
    {
        return static_cast<uint16_t>(value & ARCH_MPAM_REG_PARTID_MASK);
    }

    static uint8_t arch_mpam_reg_pmg(uint32_t value)
    {
        return static_cast<uint8_t>((value >> ARCH_MPAM_REG_PMG_SHIFT) &
                                    ARCH_MPAM_REG_PMG_MASK);
    }

    static uint8_t arch_mpam_partid_space_for_security_state(uint8_t security_state)
    {
        switch (security_state & ARCH_SECURITY_EVENTQ_STATE_MASK) {
        case ARCH_SECURITY_SECURE:
            return ARCH_MPAM_SPACE_SECURE;
        case ARCH_SECURITY_REALM:
            return ARCH_MPAM_SPACE_REALM;
        case ARCH_SECURITY_ROOT:
            return ARCH_MPAM_SPACE_ROOT;
        case ARCH_SECURITY_NONSECURE:
        default:
            return ARCH_MPAM_SPACE_NONSECURE;
        }
    }

    uint32_t arch_gmpam_for_security_state(uint8_t security_state) const
    {
        if (arch_security_eventq_index(security_state) == ARCH_SECURITY_SECURE) {
            return m_arch_secure_gmpam;
        }

        return m_arch_gmpam;
    }

    uint32_t arch_gbpmpam_for_security_state(uint8_t security_state) const
    {
        if (arch_security_eventq_index(security_state) == ARCH_SECURITY_SECURE) {
            return m_arch_secure_gbpmpam;
        }

        return m_arch_gbpmpam;
    }

    uint32_t arch_gbpa_for_security_state(uint8_t security_state) const
    {
        if (arch_security_eventq_index(security_state) == ARCH_SECURITY_SECURE) {
            return m_arch_secure_gbpa;
        }

        return m_arch_gbpa;
    }

    void populate_arch_mpam_extension(gs::ApolloSmmuStreamIdExtension& ext,
                                      uint32_t mpam_reg) const
    {
        uint16_t partid = arch_mpam_reg_partid(mpam_reg);
        uint8_t pmg = arch_mpam_reg_pmg(mpam_reg);
        bool unknown = false;

        if (!arch_mpam_partid_supported(partid)) {
            partid = ARCH_MPAM_UNKNOWN_PARTID;
            unknown = true;
        }
        if (!arch_mpam_pmg_supported(pmg)) {
            pmg = ARCH_MPAM_UNKNOWN_PMG;
            unknown = true;
        }

        ext.mpam_valid = true;
        ext.mpam_remapped = false;
        ext.mpam_unknown = unknown;
        ext.mpam_partid_space =
            arch_mpam_partid_space_for_security_state(m_arch_last_security_state);
        ext.mpam_partid = partid;
        ext.mpam_pmg = pmg;
    }

    void populate_arch_mpam_extension_from_state(gs::ApolloSmmuStreamIdExtension& ext) const
    {
        ext.mpam_valid = true;
        ext.mpam_remapped = m_arch_last_mpam_remapped;
        ext.mpam_unknown = m_arch_last_mpam_unknown;
        ext.mpam_partid_space = m_arch_last_mpam_partid_space;
        ext.mpam_partid = m_arch_last_mpam_partid;
        ext.mpam_pmg = m_arch_last_mpam_pmg;
    }

    void reset_arch_output_attrs()
    {
        m_arch_last_output_attrs_valid = false;
        m_arch_last_output_mtcfg = false;
        m_arch_last_output_mem_type = 0;
        m_arch_last_output_shareability = 0;
        m_arch_last_output_alloc_hint = 0;
        m_arch_last_output_inst_cfg = 0;
        m_arch_last_output_priv_cfg = 0;
        m_arch_last_output_ns_cfg = 0;
    }

    void record_arch_gbpa_output_attrs(uint32_t stream_id, uint32_t gbpa,
                                       const char* path, bool write)
    {
        const bool mtcfg = arch_gbpa_mtcfg(gbpa);
        const uint8_t mem_type =
            mtcfg ? static_cast<uint8_t>(arch_gbpa_memattr(gbpa)) : 0;
        uint8_t shareability = static_cast<uint8_t>(arch_gbpa_shcfg(gbpa));

        /*
         * SMMUv3 §13.2 global bypass uses SMMU_GBPA attributes while
         * SMMUEN is clear.  Match the pinned reference model's
         * ensure_consistent_attrs() behavior for the bounded memory-type
         * encodings modeled here: Device/nC memory is forced to OSH.
         */
        if (mtcfg &&
            (mem_type == 0x0 || mem_type == 0x4 || mem_type == 0x8 ||
             mem_type == 0xc)) {
            shareability = ARCH_STE_SHCFG_OSH;
        }

        m_arch_last_output_attrs_valid = true;
        m_arch_last_output_mtcfg = mtcfg;
        m_arch_last_output_mem_type = mem_type;
        m_arch_last_output_shareability = shareability;
        m_arch_last_output_alloc_hint =
            static_cast<uint8_t>(arch_gbpa_alloccfg(gbpa));
        m_arch_last_output_inst_cfg =
            static_cast<uint8_t>(arch_gbpa_instcfg(gbpa, write));
        m_arch_last_output_priv_cfg =
            static_cast<uint8_t>(arch_gbpa_privcfg(gbpa));
        m_arch_last_output_ns_cfg = 0;

        SCP_INFO(()) << "APOLLO_SMMU_TBU: GBPA output attributes"
                     << " stream-id=0x" << std::hex << stream_id
                     << " path=" << path
                     << " mem-type=0x" << static_cast<uint32_t>(mem_type)
                     << " sh=0x" << static_cast<uint32_t>(shareability)
                     << " alloc=0x"
                     << static_cast<uint32_t>(m_arch_last_output_alloc_hint)
                     << " inst=0x"
                     << static_cast<uint32_t>(m_arch_last_output_inst_cfg)
                     << " priv=0x"
                     << static_cast<uint32_t>(m_arch_last_output_priv_cfg)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: GBPA output attributes"
                  << " stream-id=0x" << std::hex << stream_id
                  << " path=" << path
                  << " mem-type=0x" << static_cast<uint32_t>(mem_type)
                  << " sh=0x" << static_cast<uint32_t>(shareability)
                  << " alloc=0x"
                  << static_cast<uint32_t>(m_arch_last_output_alloc_hint)
                  << " inst=0x"
                  << static_cast<uint32_t>(m_arch_last_output_inst_cfg)
                  << " priv=0x"
                  << static_cast<uint32_t>(m_arch_last_output_priv_cfg)
                  << std::dec << std::endl;
    }

    void record_arch_ste_output_attrs(uint32_t stream_id, uint64_t ste1,
                                      const char* path)
    {
        const bool mtcfg = arch_ste_mtcfg(ste1);
        const uint8_t mem_type =
            mtcfg ? static_cast<uint8_t>(arch_ste_memattr(ste1)) : 0;
        uint8_t shareability = static_cast<uint8_t>(arch_ste_shcfg(ste1));

        /*
         * Arm SMMUv3 output attributes are also architecturally relevant on
         * bypass results.  Model the STE override fields explicitly so
         * functional QBox traffic carries the same attributes as translated
         * traffic.  Device/nC memory is forced to OSH, matching the pinned
         * reference model's applyOutputAttrs() behavior.
         */
        if (mtcfg && mem_type == 0) {
            shareability = ARCH_STE_SHCFG_OSH;
        }

        m_arch_last_output_attrs_valid = true;
        m_arch_last_output_mtcfg = mtcfg;
        m_arch_last_output_mem_type = mem_type;
        m_arch_last_output_shareability = shareability;
        m_arch_last_output_alloc_hint =
            static_cast<uint8_t>(arch_ste_alloccfg(ste1));
        m_arch_last_output_inst_cfg =
            static_cast<uint8_t>(arch_ste_instcfg(ste1));
        m_arch_last_output_priv_cfg =
            static_cast<uint8_t>(arch_ste_privcfg(ste1));
        m_arch_last_output_ns_cfg =
            static_cast<uint8_t>(arch_ste_nscfg(ste1));

        SCP_INFO(()) << "APOLLO_SMMU_TBU: STE output attributes"
                     << " stream-id=0x" << std::hex << stream_id
                     << " path=" << path
                     << " mem-type=0x" << static_cast<uint32_t>(mem_type)
                     << " sh=0x" << static_cast<uint32_t>(shareability)
                     << " alloc=0x"
                     << static_cast<uint32_t>(m_arch_last_output_alloc_hint)
                     << " inst=0x"
                     << static_cast<uint32_t>(m_arch_last_output_inst_cfg)
                     << " priv=0x"
                     << static_cast<uint32_t>(m_arch_last_output_priv_cfg)
                     << " ns=0x"
                     << static_cast<uint32_t>(m_arch_last_output_ns_cfg)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: STE output attributes"
                  << " stream-id=0x" << std::hex << stream_id
                  << " path=" << path
                  << " mem-type=0x" << static_cast<uint32_t>(mem_type)
                  << " sh=0x" << static_cast<uint32_t>(shareability)
                  << " alloc=0x"
                  << static_cast<uint32_t>(m_arch_last_output_alloc_hint)
                  << " inst=0x"
                  << static_cast<uint32_t>(m_arch_last_output_inst_cfg)
                  << " priv=0x"
                  << static_cast<uint32_t>(m_arch_last_output_priv_cfg)
                  << " ns=0x"
                  << static_cast<uint32_t>(m_arch_last_output_ns_cfg)
                  << std::dec << std::endl;
    }

    void populate_arch_output_attrs_extension(
        gs::ApolloSmmuStreamIdExtension& ext) const
    {
        ext.output_attrs_valid = true;
        ext.output_mtcfg = m_arch_last_output_mtcfg;
        ext.output_mem_type = m_arch_last_output_mem_type;
        ext.output_shareability = m_arch_last_output_shareability;
        ext.output_alloc_hint = m_arch_last_output_alloc_hint;
        ext.output_inst_cfg = m_arch_last_output_inst_cfg;
        ext.output_priv_cfg = m_arch_last_output_priv_cfg;
        ext.output_ns_cfg = m_arch_last_output_ns_cfg;
    }

    static uint64_t arch_gatos_par_fault(uint64_t fault_code, uint64_t reason,
                                         uint64_t fault_addr)
    {
        return 0x1ULL | ((reason & 0x3ULL) << 1) |
               ((fault_code & 0xffULL) << 4) |
               (fault_addr & ARCH_GATOS_PAR_ADDR_MASK);
    }

    uint64_t arch_gatos_fault_code(uint32_t reason) const
    {
        switch (reason) {
        case ARCH_FAULT_BAD_STREAM_ID:
            return 0x02;
        case ARCH_FAULT_STE_FETCH:
            return 0x03;
        case ARCH_FAULT_STE_INVALID:
            return 0x04;
        case ARCH_FAULT_BAD_ATS_TREQ:
        case ARCH_FAULT_TRANSL_FORBIDDEN:
        case ARCH_FAULT_UNSUPPORTED_UPSTREAM:
            return 0xfd;
        case ARCH_FAULT_ATOS_INV_STAGE:
            return 0xfe;
        case ARCH_FAULT_ATOS_INV_REQ:
            return 0xff;
        case ARCH_FAULT_STREAM_DISABLED:
            return 0x06;
        case ARCH_FAULT_BAD_SUBSTREAMID:
            return 0x08;
        case ARCH_FAULT_CD_FETCH:
            return 0x09;
        case ARCH_FAULT_CD_INVALID:
            return 0x0a;
        case ARCH_FAULT_WALK_EABT:
        case ARCH_FAULT_VMS_FETCH:
            return reason == ARCH_FAULT_WALK_EABT ? 0x0b : 0x25;
        case ARCH_FAULT_ADDR_SIZE:
            return 0x11;
        case ARCH_FAULT_ACCESS:
            return 0x12;
        case ARCH_FAULT_PERMISSION:
            return 0x13;
        case ARCH_FAULT_TLB_CONFLICT:
            return 0x20;
        case ARCH_FAULT_CFG_CONFLICT:
            return 0x21;
        default:
            return 0x10;
        }
    }

    uint64_t arch_gatos_fault_reason_field() const
    {
        if (m_arch_fault_stage != ARCH_FAULT_STAGE_S2) {
            return 0;
        }
        switch (m_arch_fault_event_class) {
        case ARCH_EVENT_CLASS_CD:
            return 1;
        case ARCH_EVENT_CLASS_TT:
            return 2;
        case ARCH_EVENT_CLASS_IN:
        default:
            return 3;
        }
    }

    uint64_t arch_gatos_fault_addr_field() const
    {
        if (m_arch_fault_stage != ARCH_FAULT_STAGE_S2) {
            return 0;
        }
        return m_arch_last_ipa & ARCH_GATOS_PAR_ADDR_MASK;
    }

    uint64_t arch_gatos_success_par(uint64_t pa)
    {
        uint64_t attr = 0xff;
        uint64_t shareability = 0x3;

        if (m_arch_atos_request_active && m_arch_last_output_attrs_valid) {
            m_arch_last_atos_ste_attrs_ignored = true;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: STE output attributes ignored for ATOS"
                         << " pnu=" << (m_arch_last_atos_privileged ? 1 : 0)
                         << " ind=" << (m_arch_last_atos_instruction ? 1 : 0)
                         << " rnw=" << (m_arch_last_atos_read ? 1 : 0)
                         << " type=0x" << std::hex << m_arch_atos_type
                         << " mem-type=0x"
                         << static_cast<uint32_t>(m_arch_last_output_mem_type)
                         << " sh=0x"
                         << static_cast<uint32_t>(m_arch_last_output_shareability)
                         << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: STE output attributes ignored for ATOS"
                      << " pnu=" << (m_arch_last_atos_privileged ? 1 : 0)
                      << " ind=" << (m_arch_last_atos_instruction ? 1 : 0)
                      << " rnw=" << (m_arch_last_atos_read ? 1 : 0)
                      << " type=0x" << std::hex << m_arch_atos_type
                      << " mem-type=0x"
                      << static_cast<uint32_t>(m_arch_last_output_mem_type)
                      << " sh=0x"
                      << static_cast<uint32_t>(m_arch_last_output_shareability)
                      << std::dec << std::endl;
        } else if (m_arch_last_output_attrs_valid && m_arch_last_output_mtcfg) {
            attr = m_arch_last_output_mem_type;
            shareability = m_arch_last_output_shareability & 0x3;
        }
        if (attr == 0) {
            shareability = ARCH_STE_SHCFG_OSH;
        }

        return ((attr & 0xffULL) << 56) | ((shareability & 0x3ULL) << 8) |
               (pa & ARCH_GATOS_PAR_ADDR_MASK);
    }

    uint16_t arch_vms_partid_map_entry(uint16_t virtual_partid) const
    {
        const uint32_t index = virtual_partid & ARCH_CD_VIRTUAL_PARTID_MASK;
        const uint32_t word = index / 4;
        const uint32_t shift = (index % 4) * 16;

        return static_cast<uint16_t>((m_arch_last_vms_partid_map[word] >> shift) &
                                     ARCH_CD_PARTID_MASK);
    }

    bool arch_mpam_partid_supported(uint16_t partid) const
    {
        return partid <= ARCH_MPAMIDR_PARTID_MAX;
    }

    bool arch_mpam_pmg_supported(uint8_t pmg) const
    {
        return pmg <= ARCH_MPAMIDR_PMG_MAX;
    }

    void apply_arch_mpam_range(uint16_t physical_partid)
    {
        m_arch_last_mpam_partid = physical_partid;
        if (!arch_mpam_partid_supported(physical_partid)) {
            m_arch_last_mpam_unknown = true;
            m_arch_last_mpam_partid = ARCH_MPAM_UNKNOWN_PARTID;
        }

        if (!arch_mpam_pmg_supported(m_arch_last_mpam_pmg)) {
            m_arch_last_mpam_unknown = true;
            m_arch_last_mpam_pmg = ARCH_MPAM_UNKNOWN_PMG;
        }
    }

    void reset_arch_mpam_state()
    {
        m_arch_last_ste4 = 0;
        m_arch_last_cd5 = 0;
        m_arch_last_mpam_valid = false;
        m_arch_last_mpam_remapped = false;
        m_arch_last_mpam_unknown = false;
        m_arch_last_mpam_partid_space = ARCH_MPAM_SPACE_NONSECURE;
        m_arch_last_mpam_partid = 0;
        m_arch_last_mpam_virtual_partid = 0;
        m_arch_last_mpam_pmg = 0;
    }

    void record_arch_mpam_from_cd(uint32_t stream_id, uint64_t cd5,
                                  bool nested_stage2, bool use_s1mpam)
    {
        if (!use_s1mpam) {
            return;
        }

        reset_arch_mpam_state();
        m_arch_last_cd5 = cd5;

        const uint16_t cd_partid = arch_cd_partid(cd5);
        uint16_t physical_partid = cd_partid;

        m_arch_last_mpam_valid = true;
        m_arch_last_mpam_partid_space =
            arch_mpam_partid_space_for_security_state(m_arch_last_security_state);
        m_arch_last_mpam_pmg = arch_cd_pmg(cd5);

        if (nested_stage2) {
            /*
             * IHI0070G.b §17.2 assigns nested client transactions with
             * STE.S1MPAM=1 from VMS.PARTID_MAP[CD.PARTID[4:0]] and CD.PMG.
             * The 32-entry VMS map is the first 64 bytes fetched earlier by
             * arch_fetch_vms_if_enabled().
             */
            m_arch_last_mpam_remapped = true;
            m_arch_last_mpam_virtual_partid =
                cd_partid & ARCH_CD_VIRTUAL_PARTID_MASK;
            physical_partid =
                arch_vms_partid_map_entry(m_arch_last_mpam_virtual_partid);
        } else {
            m_arch_last_mpam_virtual_partid = cd_partid;
        }

        apply_arch_mpam_range(physical_partid);

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural MPAM CD assignment"
                     << " stream-id=0x" << std::hex << stream_id
                     << " vpartid=0x" << m_arch_last_mpam_virtual_partid
                     << " partid=0x" << m_arch_last_mpam_partid
                     << " pmg=0x" << static_cast<uint32_t>(m_arch_last_mpam_pmg)
                     << " remap=" << (m_arch_last_mpam_remapped ? 1 : 0)
                     << " unknown=" << (m_arch_last_mpam_unknown ? 1 : 0)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural MPAM CD assignment"
                  << " stream-id=0x" << std::hex << stream_id
                  << " vpartid=0x" << m_arch_last_mpam_virtual_partid
                  << " partid=0x" << m_arch_last_mpam_partid
                  << " pmg=0x" << static_cast<uint32_t>(m_arch_last_mpam_pmg)
                  << " remap=" << (m_arch_last_mpam_remapped ? 1 : 0)
                  << " unknown=" << (m_arch_last_mpam_unknown ? 1 : 0)
                  << std::dec << std::endl;
    }

    void record_arch_mpam_from_ste(uint32_t stream_id, uint64_t ste4, uint64_t ste5)
    {
        reset_arch_mpam_state();
        m_arch_last_ste4 = ste4;
        m_arch_last_ste5 = ste5;
        m_arch_last_mpam_valid = true;
        m_arch_last_mpam_partid_space =
            arch_mpam_partid_space_for_security_state(m_arch_last_security_state);
        m_arch_last_mpam_pmg = arch_ste_pmg(ste5);
        const uint16_t physical_partid = arch_ste_partid(ste4);
        m_arch_last_mpam_virtual_partid = physical_partid;
        apply_arch_mpam_range(physical_partid);

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural MPAM STE assignment"
                     << " stream-id=0x" << std::hex << stream_id
                     << " partid=0x" << m_arch_last_mpam_partid
                     << " pmg=0x" << static_cast<uint32_t>(m_arch_last_mpam_pmg)
                     << " unknown=" << (m_arch_last_mpam_unknown ? 1 : 0)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural MPAM STE assignment"
                  << " stream-id=0x" << std::hex << stream_id
                  << " partid=0x" << m_arch_last_mpam_partid
                  << " pmg=0x" << static_cast<uint32_t>(m_arch_last_mpam_pmg)
                  << " unknown=" << (m_arch_last_mpam_unknown ? 1 : 0)
                  << std::dec << std::endl;
    }

    void record_arch_mpam_from_gbp(uint32_t stream_id)
    {
        reset_arch_mpam_state();
        const uint32_t gbpmpam =
            arch_gbpmpam_for_security_state(m_arch_last_security_state);
        const uint16_t physical_partid = arch_mpam_reg_partid(gbpmpam);

        m_arch_last_mpam_valid = true;
        m_arch_last_mpam_partid_space =
            arch_mpam_partid_space_for_security_state(m_arch_last_security_state);
        m_arch_last_mpam_pmg = arch_mpam_reg_pmg(gbpmpam);
        m_arch_last_mpam_virtual_partid = physical_partid;
        apply_arch_mpam_range(physical_partid);

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural MPAM GBP assignment"
                     << " stream-id=0x" << std::hex << stream_id
                     << " partid=0x" << m_arch_last_mpam_partid
                     << " pmg=0x" << static_cast<uint32_t>(m_arch_last_mpam_pmg)
                     << " unknown=" << (m_arch_last_mpam_unknown ? 1 : 0)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural MPAM GBP assignment"
                  << " stream-id=0x" << std::hex << stream_id
                  << " partid=0x" << m_arch_last_mpam_partid
                  << " pmg=0x" << static_cast<uint32_t>(m_arch_last_mpam_pmg)
                  << " unknown=" << (m_arch_last_mpam_unknown ? 1 : 0)
                  << std::dec << std::endl;
    }

    void write_arch_mpam_update_reg(uint32_t& reg, uint32_t value)
    {
        if ((reg & ARCH_MPAM_UPDATE) != 0 || (value & ARCH_MPAM_UPDATE) == 0) {
            return;
        }

        /*
         * The compliance-slice model accepts the architected Update handshake
         * synchronously: software writes Update=1 with new PARTID/PMG fields,
         * and future reads/transactions observe the accepted value with
         * Update already cleared.
         */
        reg = value & ARCH_MPAM_REG_VALUE_MASK;
    }

    uint32_t arch_mpam_status() const
    {
        return (m_arch_last_mpam_valid ? 1u : 0u) |
               (m_arch_last_mpam_remapped ? (1u << 1) : 0u) |
               (m_arch_last_mpam_unknown ? (1u << 2) : 0u) |
               ((static_cast<uint32_t>(m_arch_last_mpam_partid_space) & 0x3u) << 6) |
               ((static_cast<uint32_t>(m_arch_last_mpam_virtual_partid) &
                 ARCH_CD_VIRTUAL_PARTID_MASK) << 8) |
               (static_cast<uint32_t>(m_arch_last_mpam_pmg) << 16);
    }

    uint32_t arch_effective_eats(uint64_t ste0, uint64_t ste1) const
    {
        const uint32_t eats =
            apollo::smmuv3::apollo_smmu_arch_core::effective_eats(
            ste0, ste1, arch_atschk_enabled());

        /*
         * SMMUv3 defines STE.EATS==0b11 as "Use DPT" only when the
         * corresponding SMMU_IDR3.DPT bit is implemented.  QBox still keeps
         * IDR3.DPT clear, so the DPT encoding behaves as EATS disabled rather
         * than as a real failed DPT lookup.
         */
        if (eats == ARCH_STE_EATS_DPT && !arch_dpt_supported()) {
            return ARCH_STE_EATS_DISABLED;
        }
        return eats;
    }

    bool arch_fetch_vms_if_enabled(uint32_t stream_id, uint64_t ste_pa,
                                   uint64_t ste0, uint64_t ste1)
    {
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_vms_partid_map.fill(0);

        if ((ste0 & ARCH_STE_VALID) == 0 ||
            arch_ste_config(ste0) != ARCH_STE_CFG_NESTED ||
            !arch_ste_s1mpam(ste1)) {
            return true;
        }

        const uint64_t ste5_pa = ste_pa + ARCH_STE_VMSPTR_OFFSET;
        uint64_t ste5 = 0;
        if (!read_downstream_u64(ste5_pa, ste5, true)) {
            set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1, ste5_pa);
            return false;
        }

        m_arch_last_ste5 = ste5;
        const uint64_t vms_ptr = arch_ste_vmsptr(ste5);
        m_arch_last_vms_ptr = vms_ptr;
        if (vms_ptr == 0) {
            return true;
        }

        for (size_t i = 0; i < m_arch_last_vms_partid_map.size(); i++) {
            const uint64_t partid_pa = vms_ptr + i * sizeof(uint64_t);

            if (!read_downstream_u64(partid_pa, m_arch_last_vms_partid_map[i], true)) {
                /*
                 * SMMUv3.2 permits the SMMU to fetch a VMS when STE.VMSPtr is
                 * active.  An External abort on that access is reported as
                 * F_VMS_FETCH, distinct from the STE word fetch above.
                 */
                set_arch_fetch_fault(ARCH_FAULT_VMS_FETCH, ARCH_FAULT_STAGE_S1, partid_pa);
                return false;
            }
        }

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural VMS fetch"
                     << " stream-id=0x" << std::hex << stream_id
                     << " ste-pa=0x" << ste_pa << " vms=0x" << vms_ptr
                     << " partid0=0x" << m_arch_last_vms_partid_map[0]
                     << " partid7=0x" << m_arch_last_vms_partid_map.back()
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural VMS fetch"
                  << " stream-id=0x" << std::hex << stream_id
                  << " ste-pa=0x" << ste_pa << " vms=0x" << vms_ptr
                  << " partid0=0x" << m_arch_last_vms_partid_map[0]
                  << " partid7=0x" << m_arch_last_vms_partid_map.back()
                  << std::dec << std::endl;
        return true;
    }

    static bool arch_cd_index_valid(uint32_t ssid, uint32_t s1cdmax)
    {
        return apollo::smmuv3::apollo_smmu_arch_core::cd_index_valid(ssid, s1cdmax);
    }

    uint32_t arch_cd_detail() const
    {
        return (m_arch_last_cd_ssid & ARCH_CMDQ_SSID_MASK) |
               ((m_arch_last_s1dss & ARCH_STE_S1DSS_MASK) << 20) |
               ((m_arch_last_s1cdmax & ARCH_STE_S1CDMAX_MASK) << 22) |
               ((m_arch_last_s1fmt & ARCH_STE_S1FMT_MASK) << 27) |
               (m_arch_last_cd_l2 ? (1u << 29) : 0u) |
               (m_arch_selected_ssid_valid ? (1u << 30) : 0u) |
               (m_arch_last_cd_bypass ? (1u << 31) : 0u);
    }

    bool arch_cd_address(uint32_t stream_id, uint64_t ste0, uint64_t ste1, uint64_t& cd_pa,
                         bool& bypass, bool stage2_translate_l1cd_fetch = false,
                         uint64_t s2ttb = 0, const arch_walk_config* s2_cfg = nullptr)
    {
        const uint32_t s1fmt = arch_ste_s1fmt(ste0);
        const uint32_t s1cdmax = arch_ste_s1cdmax(ste0);
        const uint32_t s1dss = arch_ste_s1dss(ste1);
        uint32_t ssid = m_arch_selected_ssid_valid ? m_arch_selected_ssid : 0;

        cd_pa = 0;
        bypass = false;
        m_arch_last_cd_ssid = ssid;
        m_arch_last_s1cdmax = s1cdmax;
        m_arch_last_s1dss = s1dss;
        m_arch_last_s1fmt = s1fmt;
        m_arch_last_eats = arch_effective_eats(ste0, ste1);
        m_arch_last_cd_l2 = false;
        m_arch_last_cd_bypass = false;
        m_arch_last_cd_pa = 0;

        if (s1dss > ARCH_STE_S1DSS_SSID0) {
            m_arch_fault_reason = ARCH_FAULT_CD_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: illegal S1DSS encoding"
                         << " stream-id=0x" << std::hex << stream_id
                         << " s1dss=0x" << s1dss << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: illegal S1DSS encoding"
                      << " stream-id=0x" << std::hex << stream_id
                      << " s1dss=0x" << s1dss << std::dec << std::endl;
            return false;
        }

        const bool explicit_cd_table = m_arch_selected_ssid_valid || s1cdmax != 0 ||
                                       s1fmt != ARCH_STE_S1FMT_LINEAR;
        if (!m_arch_selected_ssid_valid && explicit_cd_table) {
            if (s1dss == ARCH_STE_S1DSS_SSID0) {
                ssid = 0;
            } else if (s1dss == ARCH_STE_S1DSS_BYPASS) {
                bypass = true;
                m_arch_last_cd_bypass = true;
                SCP_INFO(()) << "APOLLO_SMMU_TBU: no-SSID context descriptor bypass"
                             << " stream-id=0x" << std::hex << stream_id
                             << " s1dss=0x" << s1dss << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: no-SSID context descriptor bypass"
                          << " stream-id=0x" << std::hex << stream_id
                          << " s1dss=0x" << s1dss << std::dec << std::endl;
                return true;
            } else {
                m_arch_fault_reason = ARCH_FAULT_STREAM_DISABLED;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
                SCP_WARN(()) << "APOLLO_SMMU_TBU: no-SSID context descriptor terminated"
                             << " stream-id=0x" << std::hex << stream_id
                             << " s1dss=0x" << s1dss << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: no-SSID context descriptor terminated"
                          << " stream-id=0x" << std::hex << stream_id
                          << " s1dss=0x" << s1dss << std::dec << std::endl;
                return false;
            }
        }

        m_arch_last_cd_ssid = ssid;
        if (m_arch_selected_ssid_valid && s1cdmax == 0) {
            m_arch_fault_reason = ARCH_FAULT_BAD_SUBSTREAMID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: substream supplied while disabled"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ssid=0x" << ssid << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: substream supplied while disabled"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ssid=0x" << ssid << std::dec << std::endl;
            return false;
        }
        if (m_arch_selected_ssid_valid && ssid == 0 && s1cdmax != 0 &&
            s1dss == ARCH_STE_S1DSS_SSID0) {
            m_arch_fault_reason = ARCH_FAULT_STREAM_DISABLED;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: substream zero disabled by S1DSS"
                         << " stream-id=0x" << std::hex << stream_id << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: substream zero disabled by S1DSS"
                      << " stream-id=0x" << std::hex << stream_id << std::dec << std::endl;
            return false;
        }
        if (!arch_cd_index_valid(ssid, s1cdmax)) {
            m_arch_fault_reason = ARCH_FAULT_BAD_SUBSTREAMID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: context descriptor SSID out of S1CDMax"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ssid=0x" << ssid << " s1cdmax=0x" << s1cdmax << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: context descriptor SSID out of S1CDMax"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ssid=0x" << ssid << " s1cdmax=0x" << s1cdmax << std::dec
                      << std::endl;
            return false;
        }

        if (s1fmt == ARCH_STE_S1FMT_LINEAR) {
            cd_pa = m_arch_cd_base + static_cast<uint64_t>(ssid) * ARCH_CD_SIZE;
            m_arch_last_cd_pa = cd_pa;
            return true;
        }

        if (s1fmt != ARCH_STE_S1FMT_64K_L2) {
            m_arch_fault_reason = ARCH_FAULT_CD_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            return false;
        }

        const uint32_t l1_index = ssid / ARCH_CD_L2_ENTRIES;
        const uint32_t l2_index = ssid % ARCH_CD_L2_ENTRIES;
        const uint64_t l1_pa = m_arch_cd_base + static_cast<uint64_t>(l1_index) * sizeof(uint64_t);
        uint64_t l1_fetch_pa = l1_pa;
        uint64_t l1_desc = 0;

        if (stage2_translate_l1cd_fetch) {
            uint64_t l1_s2_desc = 0;

            m_arch_fault_event_class = ARCH_EVENT_CLASS_CD;
            m_arch_last_ipa = l1_pa;
            if (s2ttb == 0 || s2_cfg == nullptr) {
                m_arch_fault_reason = ARCH_FAULT_STAGE2;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                return false;
            }
            if (!arch_descriptor_walk(stream_id, l1_pa, s2ttb, *s2_cfg, false,
                                      l1_fetch_pa, l1_s2_desc)) {
                m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                m_arch_fault_event_class = ARCH_EVENT_CLASS_CD;
                m_arch_last_ipa = l1_pa;
                return false;
            }
            if (arch_s2ptw_reject_device_fetch(stream_id, "L1CD fetch",
                                               l1_pa, l1_fetch_pa,
                                               l1_s2_desc,
                                               ARCH_EVENT_CLASS_CD)) {
                return false;
            }
            m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural nested L1CD fetch stage-2 walk"
                         << " stream-id=0x" << std::hex << stream_id
                         << " l1cd-ipa=0x" << l1_pa << " l1cd-pa=0x" << l1_fetch_pa
                         << " s2ttb=0x" << s2ttb << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural nested L1CD fetch stage-2 walk"
                      << " stream-id=0x" << std::hex << stream_id
                      << " l1cd-ipa=0x" << l1_pa << " l1cd-pa=0x" << l1_fetch_pa
                      << " s2ttb=0x" << s2ttb << std::dec << std::endl;
        }

        if (!read_downstream_u64(l1_fetch_pa, l1_desc, false, true)) {
            set_arch_fetch_fault(ARCH_FAULT_CD_FETCH, ARCH_FAULT_STAGE_S1, l1_fetch_pa);
            return false;
        }
        if (arch_has_reserved_bits(l1_desc, ARCH_CD_L1_DESC_MODELED_MASK)) {
            m_arch_fault_reason = ARCH_FAULT_CD_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: reserved CD L1 encoding"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ssid=0x" << ssid << " l1-pa=0x" << l1_fetch_pa
                         << " desc=0x" << l1_desc << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: reserved CD L1 encoding"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ssid=0x" << ssid << " l1-pa=0x" << l1_fetch_pa
                      << " desc=0x" << l1_desc << std::dec << std::endl;
            return false;
        }
        if ((l1_desc & ARCH_CD_L1_DESC_VALID) == 0 ||
            (l1_desc & ARCH_CD_L1_DESC_L2PTR_MASK) == 0) {
            m_arch_fault_reason = ARCH_FAULT_BAD_SUBSTREAMID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid context descriptor L1 entry"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ssid=0x" << ssid << " l1-pa=0x" << l1_fetch_pa
                         << " desc=0x" << l1_desc << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: invalid context descriptor L1 entry"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ssid=0x" << ssid << " l1-pa=0x" << l1_fetch_pa
                      << " desc=0x" << l1_desc << std::dec << std::endl;
            return false;
        }

        const uint64_t l2_base = l1_desc & ARCH_CD_L1_DESC_L2PTR_MASK;
        cd_pa = l2_base + static_cast<uint64_t>(l2_index) * ARCH_CD_SIZE;
        m_arch_last_cd_l2 = true;
        m_arch_last_cd_pa = cd_pa;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural 2-level context descriptor table walk"
                     << " stream-id=0x" << std::hex << stream_id
                     << " ssid=0x" << ssid << " l1-index=0x" << l1_index
                     << " l2-index=0x" << l2_index << " l1-desc=0x" << l1_desc
                     << " cd-pa=0x" << cd_pa << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural 2-level context descriptor table walk"
                  << " stream-id=0x" << std::hex << stream_id
                  << " ssid=0x" << ssid << " l1-index=0x" << l1_index
                  << " l2-index=0x" << l2_index << " l1-desc=0x" << l1_desc
                  << " cd-pa=0x" << cd_pa << std::dec << std::endl;
        return true;
    }

    bool arch_ste_address(uint32_t stream_id, uint64_t& ste_pa)
    {
        const uint8_t security_state =
            arch_security_eventq_index(m_arch_last_security_state);
        arch_security_strtab_bank* security_bank = nullptr;
        uint64_t selected_strtab_base = m_arch_strtab_base;
        uint32_t selected_strtab_cfg = m_arch_strtab_cfg;

        if (arch_security_strtab_uses_bank(security_state)) {
            security_bank = &m_arch_security_strtab_banks[security_state];
            selected_strtab_base = security_bank->base;
            selected_strtab_cfg = security_bank->cfg;
        }

        if (selected_strtab_base == 0) {
            if (m_arch_ste_base == 0) {
                set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1, 0);
                return false;
            }
            ste_pa = m_arch_ste_base + static_cast<uint64_t>(stream_id) * ARCH_STE_SIZE;
            return true;
        }

        if (!arch_stream_id_in_bounds(selected_strtab_cfg, stream_id)) {
            m_arch_fault_reason = ARCH_FAULT_BAD_STREAM_ID;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: bad StreamID for architectural stream table stream-id=0x"
                         << std::hex << stream_id << " cfg=0x" << selected_strtab_cfg
                         << " security-state=0x" << static_cast<uint32_t>(security_state)
                         << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: bad StreamID for architectural stream table stream-id=0x"
                      << std::hex << stream_id << " cfg=0x" << selected_strtab_cfg
                      << " security-state=0x" << static_cast<uint32_t>(security_state)
                      << std::dec << std::endl;
            return false;
        }

        const uint64_t strtab_base =
            apollo::smmuv3::apollo_smmu_arch_core::stream_table_base_addr(
                selected_strtab_base);
        const uint32_t fmt = arch_strtab_format(selected_strtab_cfg);

        if (fmt == ARCH_STRTAB_FMT_LINEAR) {
            ste_pa = strtab_base + static_cast<uint64_t>(stream_id) * ARCH_STE_SIZE;
            if (security_bank != nullptr) {
                security_bank->last_stream_id = stream_id;
                security_bank->last_ste_pa = ste_pa;
                security_bank->lookups++;
                security_bank->valid = true;
            }
            return true;
        }

        if (fmt != ARCH_STRTAB_FMT_2LVL) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            return false;
        }

        const uint32_t split = arch_strtab_split(selected_strtab_cfg);
        if (!apollo::smmuv3::apollo_smmu_arch_core::stream_table_split_valid(
                selected_strtab_cfg)) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            return false;
        }

        const uint32_t l1_index =
            apollo::smmuv3::apollo_smmu_arch_core::stream_table_l1_index(stream_id,
                                                                         split);
        const uint32_t l2_index =
            apollo::smmuv3::apollo_smmu_arch_core::stream_table_l2_index(stream_id,
                                                                         split);
        uint64_t l1_desc = 0;
        const uint64_t l1_pa = strtab_base + static_cast<uint64_t>(l1_index) * sizeof(l1_desc);

        if (!read_downstream_u64(l1_pa, l1_desc, true)) {
            set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1, l1_pa);
            return false;
        }

        const uint64_t l2_base =
            apollo::smmuv3::apollo_smmu_arch_core::stream_table_l1_l2_base(l1_desc);

        if (!apollo::smmuv3::apollo_smmu_arch_core::stream_table_l1_desc_valid(
                l1_desc, split, l2_index)) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural L1 stream descriptor stream-id=0x"
                         << std::hex << stream_id << " l1-pa=0x" << l1_pa << " desc=0x" << l1_desc
                         << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: invalid architectural L1 stream descriptor stream-id=0x"
                      << std::hex << stream_id << " l1-pa=0x" << l1_pa << " desc=0x" << l1_desc
                      << std::dec << std::endl;
            return false;
        }

        ste_pa = l2_base + static_cast<uint64_t>(l2_index) * ARCH_STE_SIZE;
        if (security_bank != nullptr) {
            security_bank->last_stream_id = stream_id;
            security_bank->last_ste_pa = ste_pa;
            security_bank->lookups++;
            security_bank->valid = true;
        }
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural 2-level stream table walk stream-id=0x"
                     << std::hex << stream_id << " l1-index=0x" << l1_index
                     << " l2-index=0x" << l2_index << " l1-desc=0x" << l1_desc
                     << " ste-pa=0x" << ste_pa
                     << " security-state=0x" << static_cast<uint32_t>(security_state)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural 2-level stream table walk stream-id=0x"
                  << std::hex << stream_id << " l1-index=0x" << l1_index << " l2-index=0x"
                  << l2_index << " l1-desc=0x" << l1_desc << " ste-pa=0x" << ste_pa
                  << " security-state=0x" << static_cast<uint32_t>(security_state)
                  << std::dec << std::endl;
        return true;
    }

    bool arch_descriptor_walk(uint32_t stream_id, uint64_t iova, uint64_t table_pa,
                              const arch_walk_config& cfg, bool write, uint64_t& pa,
                              uint64_t& desc, bool stage2_translate_descriptor_fetch = false,
                              uint64_t s2ttb = 0,
                              const arch_walk_config* s2_cfg = nullptr,
                              uint64_t secure_s2ttb = 0,
                              bool stage1_secure_stream = false,
                              bool stage1_table_walk_nonsecure = true,
                              bool* stage1_output_nonsecure_ipa = nullptr,
                              bool stage1_hier_attrs_disabled = false)
    {
        using arch_core = apollo::smmuv3::apollo_smmu_arch_core;
        bool s1_nonsecure = stage1_table_walk_nonsecure;
        bool s1_hier_no_unpriv = false;
        bool s1_hier_ro = false;
        bool s1_hier_uxn = false;
        bool s1_hier_pxn = false;

        const arch_core::descriptor_walk_config core_cfg {
            cfg.granule,
            cfg.start_level,
            cfg.levels,
            cfg.stage,
        };

        if (stage1_output_nonsecure_ipa != nullptr) {
            *stage1_output_nonsecure_ipa = s1_nonsecure || !stage1_secure_stream;
        }
        if (!m_arch_core.begin_descriptor_walk(iova, table_pa, core_cfg)) {
            return false;
        }

        for (uint32_t level = cfg.start_level; level < cfg.levels; level++) {
            const auto fetch =
                m_arch_core.begin_descriptor_fetch(table_pa, iova, core_cfg, level);
            const uint64_t desc_pa = fetch.desc_pa;
            uint64_t desc_fetch_pa = desc_pa;
            bool desc_fetch_stage2_translated = false;
            if (stage2_translate_descriptor_fetch && cfg.stage == ARCH_FAULT_STAGE_S1) {
                uint64_t fetch_s2_desc = 0;
                const bool fetch_secure_ipa = stage1_secure_stream && !s1_nonsecure;
                const uint64_t fetch_s2ttb =
                    fetch_secure_ipa ? secure_s2ttb : s2ttb;

                m_arch_fault_event_class = ARCH_EVENT_CLASS_TT;
                m_arch_last_ipa = desc_pa;
                m_arch_last_s1_tt_fetch_secure_ipa = fetch_secure_ipa;
                m_arch_last_s1_tt_fetch_s2ttb = fetch_s2ttb;
                if (fetch_s2ttb == 0 || s2_cfg == nullptr) {
                    m_arch_fault_reason = ARCH_FAULT_STAGE2;
                    m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                    return false;
                }
                if (!arch_descriptor_walk(stream_id, desc_pa, fetch_s2ttb, *s2_cfg, false,
                                          desc_fetch_pa, fetch_s2_desc)) {
                    desc = fetch_s2_desc;
                    m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                    m_arch_fault_event_class = ARCH_EVENT_CLASS_TT;
                    m_arch_fault_nsipa = stage1_secure_stream && s1_nonsecure;
                    m_arch_last_ipa = desc_pa;
                    return false;
                }
                if (arch_desc_s2_memattr_is_device(fetch_s2_desc)) {
                    m_arch_last_ptwnnc_device_fetch = true;
                    if (arch_s2ptw_reject_device_fetch(
                            stream_id, "stage-1 descriptor fetch",
                            desc_pa, desc_fetch_pa, fetch_s2_desc,
                            ARCH_EVENT_CLASS_TT)) {
                        return false;
                    }
                    if ((ARCH_IDR3 & ARCH_IDR3_PTWNNC) != 0) {
                        m_arch_last_ptwnnc_normalized = true;
                        SCP_INFO(()) << "APOLLO_SMMU_TBU: PTWNNC normalizes"
                                     << " stage-1 descriptor fetch through"
                                     << " Device-mapped stage-2 memory"
                                     << " stream-id=0x" << std::hex << stream_id
                                     << " tt-ipa=0x" << desc_pa
                                     << " tt-pa=0x" << desc_fetch_pa
                                     << " s2-desc=0x" << fetch_s2_desc << std::dec;
                        std::cerr << "APOLLO_SMMU_TBU: PTWNNC normalizes"
                                  << " stage-1 descriptor fetch through"
                                  << " Device-mapped stage-2 memory"
                                  << " stream-id=0x" << std::hex << stream_id
                                  << " tt-ipa=0x" << desc_pa
                                  << " tt-pa=0x" << desc_fetch_pa
                                  << " s2-desc=0x" << fetch_s2_desc << std::dec
                                  << std::endl;
                    }
                }
                desc_fetch_stage2_translated = true;
                m_arch_fault_stage = cfg.stage;
                m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
                SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural nested stage-1 TT fetch stage-2 walk"
                             << " stream-id=0x" << std::hex << stream_id
                             << " tt-ipa=0x" << desc_pa
                             << " tt-pa=0x" << desc_fetch_pa
                             << " secure-ipa=" << (fetch_secure_ipa ? 1 : 0)
                             << " s2ttb=0x" << fetch_s2ttb << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architectural nested stage-1 TT fetch stage-2 walk"
                          << " stream-id=0x" << std::hex << stream_id
                          << " tt-ipa=0x" << desc_pa
                          << " tt-pa=0x" << desc_fetch_pa
                          << " secure-ipa=" << (fetch_secure_ipa ? 1 : 0)
                          << " s2ttb=0x" << fetch_s2ttb << std::dec << std::endl;
            }
            const auto desc_read = m_arch_core.begin_descriptor_memory_read(
                fetch, desc_fetch_pa, core_cfg, desc_fetch_stage2_translated);
            if (!execute_descriptor_memory_read(desc_read, desc)) {
                m_arch_core.fail_descriptor_memory_read(desc_read);
                return false;
            }
            if (!arch_apply_httu_table_update(stream_id, desc_pa, desc_fetch_pa,
                                              desc, cfg, level)) {
                m_arch_core.fail_descriptor_memory_read(desc_read, true);
                return false;
            }
            if (!arch_apply_httu_leaf_update(stream_id, desc_pa, desc_fetch_pa,
                                             desc, cfg, level, write)) {
                m_arch_core.fail_descriptor_memory_read(desc_read, true);
                return false;
            }
            m_arch_core.complete_descriptor_memory_read(desc_read, desc);

            const auto step = m_arch_core.evaluate_descriptor_step(
                iova, desc_pa, desc, core_cfg, level, write);
            if (step.kind == arch_core::descriptor_step_kind::fault) {
                if (m_arch_fault_reason == ARCH_FAULT_TABLE_INVALID) {
                    SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural table descriptor stream-id=0x"
                                 << std::hex << stream_id << " level=" << std::dec << level
                                 << std::hex << " desc-pa=0x" << desc_pa << " desc=0x"
                                 << desc;
                    std::cerr << "APOLLO_SMMU_TBU: invalid architectural table descriptor stream-id=0x"
                              << std::hex << stream_id << " level=" << std::dec << level
                              << std::hex << " desc-pa=0x" << desc_pa << " desc=0x"
                              << desc << std::dec << std::endl;
                } else if (m_arch_fault_reason == ARCH_FAULT_PAGE_INVALID) {
                    SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural page descriptor stream-id=0x"
                                 << std::hex << stream_id << " desc-pa=0x" << desc_pa
                                 << " desc=0x" << desc;
                    std::cerr << "APOLLO_SMMU_TBU: invalid architectural page descriptor stream-id=0x"
                              << std::hex << stream_id << " desc-pa=0x" << desc_pa
                              << " desc=0x" << desc << std::dec << std::endl;
                }
                return false;
            }

            if (step.kind == arch_core::descriptor_step_kind::leaf) {
                if (cfg.stage == ARCH_FAULT_STAGE_S1) {
                    const bool privileged = arch_stage1_access_privileged();
                    const bool instruction = arch_access_instruction();

                    if (!privileged && s1_hier_no_unpriv) {
                        m_arch_last_hier_attrs_applied = true;
                        m_arch_fault_reason = ARCH_FAULT_PERMISSION;
                        m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
                        return false;
                    }
                    if (write && s1_hier_ro) {
                        m_arch_last_hier_attrs_applied = true;
                        m_arch_fault_reason = ARCH_FAULT_PERMISSION;
                        m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
                        return false;
                    }
                    if (instruction &&
                        ((privileged && s1_hier_pxn) ||
                         (!privileged && s1_hier_uxn))) {
                        m_arch_last_hier_attrs_applied = true;
                        m_arch_fault_reason = ARCH_FAULT_PERMISSION;
                        m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
                        return false;
                    }
                }
                if (cfg.stage == ARCH_FAULT_STAGE_S2 &&
                    arch_access_instruction()) {
                    const bool privileged = arch_stage1_access_privileged();
                    const bool privileged_xn = (desc & ARCH_DESC_PXN) != 0;
                    const bool unprivileged_xn = (desc & ARCH_DESC_UXN) != 0;

                    if ((ARCH_IDR3 & ARCH_IDR3_XNX) != 0 &&
                        ((privileged && privileged_xn) ||
                         (!privileged && unprivileged_xn))) {
                        m_arch_last_xnx_fault = true;
                        m_arch_fault_reason = ARCH_FAULT_PERMISSION;
                        m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                        m_arch_last_stage = ARCH_FAULT_STAGE_S2;
                        SCP_WARN(()) << "APOLLO_SMMU_TBU: IDR3.XNX stage-2"
                                     << " execute-never permission fault"
                                     << " stream-id=0x" << std::hex << stream_id
                                     << " iova=0x" << iova
                                     << " desc=0x" << desc
                                     << " privileged=" << (privileged ? 1 : 0)
                                     << std::dec;
                        std::cerr << "APOLLO_SMMU_TBU: IDR3.XNX stage-2"
                                  << " execute-never permission fault"
                                  << " stream-id=0x" << std::hex << stream_id
                                  << " iova=0x" << iova
                                  << " desc=0x" << desc
                                  << " privileged=" << (privileged ? 1 : 0)
                                  << std::dec << std::endl;
                        return false;
                    }
                }
                if (step.block_nt &&
                    ((ARCH_IDR3 & ARCH_IDR3_BBML_MASK) ==
                     ARCH_IDR3_BBML_LEVEL_2)) {
                    m_arch_last_bbml2_nt_ignored = true;
                    m_arch_last_bbml2_nt_desc = desc;
                    SCP_INFO(()) << "APOLLO_SMMU_TBU: IDR3.BBML level-2"
                                 << " ignores block descriptor nT"
                                 << " stream-id=0x" << std::hex << stream_id
                                 << " iova=0x" << iova
                                 << " desc=0x" << desc
                                 << " level=" << std::dec << level;
                    std::cerr << "APOLLO_SMMU_TBU: IDR3.BBML level-2"
                              << " ignores block descriptor nT"
                              << " stream-id=0x" << std::hex << stream_id
                              << " iova=0x" << iova
                              << " desc=0x" << desc
                              << " level=" << std::dec << level << std::endl;
                }
                if (cfg.stage == ARCH_FAULT_STAGE_S1) {
                    const bool leaf_nonsecure = (desc & ARCH_DESC_NS) != 0;
                    const bool output_nonsecure =
                        !stage1_secure_stream || s1_nonsecure || leaf_nonsecure;

                    m_arch_last_s1_table_walk_nonsecure = s1_nonsecure;
                    m_arch_last_s1_output_nonsecure_ipa = output_nonsecure;
                    if (stage1_output_nonsecure_ipa != nullptr) {
                        *stage1_output_nonsecure_ipa = output_nonsecure;
                    }
                }
                pa = step.pa;
                ats_fill(stream_id, iova & ~arch_granule_mask(cfg.granule),
                         m_arch_current_asid, m_arch_current_vmid,
                         m_arch_current_ssid_valid, m_arch_current_ssid,
                         m_arch_last_security_state, level,
                         arch_granule_size(cfg.granule));
                SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural descriptor walk stream-id=0x" << std::hex
                             << stream_id << " stage=" << std::dec << cfg.stage
                             << " granule=0x" << std::hex << arch_granule_size(cfg.granule)
                             << " start-level=" << std::dec << cfg.start_level
                             << " leaf-level=" << level << " levels=" << m_arch_walk_depth << std::hex
                             << " desc-pa=0x" << desc_pa << " desc=0x" << desc
                             << " iova=0x" << iova << " pa=0x" << pa << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architectural descriptor walk stream-id=0x" << std::hex
                          << stream_id << " stage=" << std::dec << cfg.stage
                          << " granule=0x" << std::hex << arch_granule_size(cfg.granule)
                          << " start-level=" << std::dec << cfg.start_level
                          << " leaf-level=" << level << " levels=" << m_arch_walk_depth << std::hex
                          << " desc-pa=0x" << desc_pa << " desc=0x" << desc
                          << " iova=0x" << iova << " pa=0x" << pa << std::dec << std::endl;
                return true;
            }

            if (step.kind == arch_core::descriptor_step_kind::table) {
                if (cfg.stage == ARCH_FAULT_STAGE_S1) {
                    const bool has_hier_attrs = arch_table_desc_has_hier_attrs(desc);

                    if (stage1_hier_attrs_disabled) {
                        m_arch_last_had_disabled_hier_attrs |= has_hier_attrs;
                    } else {
                        s1_hier_no_unpriv |=
                            (desc & ARCH_DESC_APTABLE_NO_UNPRIV) != 0;
                        s1_hier_ro |= (desc & ARCH_DESC_APTABLE_RO) != 0;
                        s1_hier_uxn |= (desc & ARCH_DESC_UXNTABLE) != 0;
                        s1_hier_pxn |= (desc & ARCH_DESC_PXNTABLE) != 0;
                    }
                }
                if (cfg.stage == ARCH_FAULT_STAGE_S1 &&
                    stage1_secure_stream &&
                    (desc & ARCH_DESC_NSTABLE) != 0) {
                    s1_nonsecure = true;
                    m_arch_last_s1_table_walk_nonsecure = true;
                    if (stage1_output_nonsecure_ipa != nullptr) {
                        *stage1_output_nonsecure_ipa = true;
                    }
                }
                table_pa = step.next_table_pa;
                SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural table walk stream-id=0x" << std::hex
                             << stream_id << " stage=" << std::dec << cfg.stage
                             << " granule=0x" << std::hex << arch_granule_size(cfg.granule)
                             << " level=" << std::dec << level << std::hex
                             << " table=0x" << fetch.table_pa << " index=0x" << fetch.index
                             << " desc-pa=0x" << desc_pa << " desc=0x" << desc << " next-table=0x" << table_pa
                             << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architectural table walk stream-id=0x" << std::hex
                          << stream_id << " stage=" << std::dec << cfg.stage
                          << " granule=0x" << std::hex << arch_granule_size(cfg.granule)
                          << " level=" << std::dec << level << std::hex
                          << " table=0x" << fetch.table_pa << " index=0x" << fetch.index
                          << " desc-pa=0x" << desc_pa << " desc=0x" << desc << " next-table=0x" << table_pa
                          << std::dec << std::endl;
                continue;
            }
        }

        m_arch_fault_reason = ARCH_FAULT_PAGE_INVALID;
        return false;
    }

    bool arch_finish_leaf(uint32_t stream_id, uint64_t iova, uint64_t desc_pa, uint64_t desc,
                          const arch_walk_config& cfg, uint32_t level, bool write,
                          uint64_t& pa)
    {
        const uint64_t offset_mask = arch_level_offset_mask(cfg, level);
        const uint64_t output_mask = 0x0000ffffffffffffULL & ~offset_mask;

        if ((desc & ARCH_DESC_AF) == 0) {
            m_arch_fault_reason = ARCH_FAULT_ACCESS;
            return false;
        }
        if (write && (desc & ARCH_DESC_AP_RO) != 0) {
            m_arch_fault_reason = ARCH_FAULT_PERMISSION;
            return false;
        }

        pa = (desc & output_mask) | (iova & offset_mask);
        ats_fill(stream_id, iova & ~arch_granule_mask(cfg.granule), m_arch_current_asid,
                 m_arch_current_vmid, m_arch_current_ssid_valid, m_arch_current_ssid,
                 m_arch_last_security_state, level, arch_granule_size(cfg.granule));
        m_arch_walk_depth = cfg.levels - cfg.start_level;
        m_arch_last_stage = cfg.stage;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural descriptor walk stream-id=0x" << std::hex
                     << stream_id << " stage=" << std::dec << cfg.stage
                     << " granule=0x" << std::hex << arch_granule_size(cfg.granule)
                     << " start-level=" << std::dec << cfg.start_level
                     << " leaf-level=" << level << " levels=" << m_arch_walk_depth << std::hex
                     << " desc-pa=0x" << desc_pa << " desc=0x" << desc
                     << " iova=0x" << iova << " pa=0x" << pa << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural descriptor walk stream-id=0x" << std::hex
                  << stream_id << " stage=" << std::dec << cfg.stage
                  << " granule=0x" << std::hex << arch_granule_size(cfg.granule)
                  << " start-level=" << std::dec << cfg.start_level
                  << " leaf-level=" << level << " levels=" << m_arch_walk_depth << std::hex
                  << " desc-pa=0x" << desc_pa << " desc=0x" << desc
                  << " iova=0x" << iova << " pa=0x" << pa << std::dec << std::endl;
        return true;
    }

    bool arch_descriptor_walk(uint32_t stream_id, uint64_t iova, uint64_t& pa, uint64_t& desc)
    {
        return arch_descriptor_walk(stream_id, iova, m_arch_ttbr, arch_walk_config {}, false, pa,
                                    desc);
    }

    bool arch_stream_context_walk(uint32_t stream_id, uint64_t iova, uint64_t& pa, uint64_t& desc,
                                  bool write = false)
    {
        uint64_t ste0 = 0;
        uint64_t ste1 = 0;
        uint64_t ste2 = 0;
        uint64_t ste4 = 0;
        uint64_t ste5 = 0;
        uint64_t cd0 = 0;
        uint64_t cd1 = 0;
        uint64_t ste_pa = 0;

        m_arch_fault_record_suppressed = false;
        m_arch_fault_stage2_stall = false;
        m_arch_current_cd_ha = false;
        m_arch_current_cd_hd = false;
        m_arch_current_cd_haft = false;
        m_arch_current_s2_ptw = false;
        m_arch_current_s2_ha = false;
        m_arch_current_s2_hd = false;
        m_arch_current_s2_haft = false;
        m_arch_last_httu_af_update = false;
        m_arch_last_httu_dirty_update = false;
        m_arch_last_httu_table_af_update = false;
        m_arch_last_httu_desc_pa = 0;
        m_arch_last_httu_desc_before = 0;
        m_arch_last_httu_desc_after = 0;
        m_arch_last_s2ptw_fault = false;

        if (m_arch_ste_base == 0 && !arch_active_stream_table_configured()) {
            return arch_descriptor_walk(stream_id, iova, m_arch_ttbr, arch_walk_config {}, write,
                                        pa, desc);
        }

        if (!arch_ste_address(stream_id, ste_pa)) {
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            return false;
        }

        if (!read_downstream_u64(ste_pa, ste0, true) ||
            !read_downstream_u64(ste_pa + sizeof(ste0), ste1, true)) {
            set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1, ste_pa);
            return false;
        }
        if (!read_downstream_u64(ste_pa + 2 * sizeof(ste0), ste2, true)) {
            set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1,
                                 ste_pa + 2 * sizeof(ste0));
            return false;
        }
        if (arch_reject_reserved_ste(stream_id, ste_pa, ste0, ste1, ste2)) {
            return false;
        }

        m_arch_last_ste = ste0;
        m_arch_last_ste1 = ste1;
        m_arch_current_s2_ptw = arch_ste_s2ptw(ste2);
        m_arch_current_s2_ha = arch_ste_s2ha(ste2);
        m_arch_current_s2_hd = arch_ste_s2hd(ste2);
        m_arch_current_s2_haft = arch_ste_s2haft(ste2);
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_current_asid = 0;
        m_arch_current_vmid = 0;
        m_arch_current_ssid = 0;
        m_arch_current_ssid_valid = false;
        m_arch_last_cd_ssid = 0;
        m_arch_last_s1cdmax = 0;
        m_arch_last_s1dss = 0;
        m_arch_last_s1fmt = 0;
        m_arch_last_cd_l2 = false;
        m_arch_last_cd_bypass = false;
        m_arch_last_cd_pa = 0;
        m_arch_s2ttb = 0;
        m_arch_last_s_s2ttb = 0;
        m_arch_last_s2_secure_ipa = false;
        m_arch_last_nscfg = ARCH_STE_NSCFG_USE_INCOMING;
        m_arch_last_cd_nscfg0 = false;
        m_arch_last_s1_table_walk_nonsecure = true;
        m_arch_last_s1_output_nonsecure_ipa = true;
        m_arch_last_s1_tt_fetch_secure_ipa = false;
        m_arch_last_s1_tt_fetch_s2ttb = 0;
        m_arch_last_cd2 = 0;
        m_arch_last_e0pd_fault = false;
        m_arch_last_had0 = false;
        m_arch_last_had1 = false;
        m_arch_last_had_disabled_hier_attrs = false;
        m_arch_last_hier_attrs_applied = false;
        m_arch_last_xnx_fault = false;
        m_arch_last_bbml2_nt_ignored = false;
        m_arch_last_bbml2_nt_desc = 0;
        m_arch_current_cd_ha = false;
        m_arch_current_cd_hd = false;
        m_arch_current_cd_haft = false;
        m_arch_last_httu_af_update = false;
        m_arch_last_httu_dirty_update = false;
        m_arch_last_httu_table_af_update = false;
        m_arch_last_httu_desc_pa = 0;
        m_arch_last_httu_desc_before = 0;
        m_arch_last_httu_desc_after = 0;
        m_arch_last_ptwnnc_device_fetch = false;
        m_arch_last_ptwnnc_normalized = false;
        m_arch_last_s2ptw_fault = false;
        m_arch_last_ipa = 0;
        m_arch_last_fetch_addr = 0;
        m_arch_fault_nsipa = false;
        m_arch_fault_gpcf = false;
        reset_arch_mpam_state();

        const uint32_t ste_cfg = arch_ste_config(ste0);
        if (!arch_atos_validate_ste_config(stream_id, ste0, ste_pa)) {
            return false;
        }
        if (arch_ste_has_stage2(ste0)) {
            m_arch_current_vmid = arch_ste_s2vmid(ste2);
            m_arch_last_vmid = m_arch_current_vmid;
        }
        if (!arch_validate_vatos_vmid_scope(stream_id, ste_pa, ste0, ste2)) {
            return false;
        }
        if ((ste0 & ARCH_STE_VALID) != 0 && ste_cfg == 0) {
            m_arch_fault_reason = ARCH_FAULT_STREAM_DISABLED;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            m_arch_fault_record_suppressed = true;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: STE.Config disabled no-event"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ste-pa=0x" << ste_pa << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: STE.Config disabled no-event"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ste-pa=0x" << ste_pa << std::dec << std::endl;
            return false;
        }
        if (!arch_fetch_vms_if_enabled(stream_id, ste_pa, ste0, ste1)) {
            return false;
        }
        if (!read_downstream_u64(ste_pa + ARCH_STE_MPAM_WORD4_OFFSET, ste4, true)) {
            set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1,
                                 ste_pa + ARCH_STE_MPAM_WORD4_OFFSET);
            return false;
        }
        if (arch_ste_s1mpam(ste1) && m_arch_last_ste5 != 0) {
            ste5 = m_arch_last_ste5;
        } else if (!read_downstream_u64(ste_pa + ARCH_STE_VMSPTR_OFFSET, ste5, true)) {
            set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1,
                                 ste_pa + ARCH_STE_VMSPTR_OFFSET);
            return false;
        }
        record_arch_mpam_from_ste(stream_id, ste4, ste5);
        if (arch_ste_all_bypass(ste0)) {
            m_arch_last_cd = 0;
            m_arch_ttbr = 0;
            m_arch_last_asid = 0;
            m_arch_last_vmid = 0;
            m_arch_last_ipa = iova;
            record_arch_ste_output_attrs(stream_id, ste1, "ste-config-bypass");

            pa = iova;
            desc = 0;
            ats_fill(stream_id, iova & ~static_cast<uint64_t>(PAGE_SIZE - 1),
                     0, 0, false, 0, m_arch_last_security_state);
            m_arch_last_stage = ARCH_FAULT_STAGE_NONE;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural STE.Config all-bypass"
                         << " stream-id=0x" << std::hex << stream_id
                         << " iova=0x" << iova << " pa=0x" << pa << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural STE.Config all-bypass"
                      << " stream-id=0x" << std::hex << stream_id
                      << " iova=0x" << iova << " pa=0x" << pa << std::dec
                      << std::endl;
            return true;
        }
        if ((ste0 & ARCH_STE_VALID) != 0 && ste_cfg == ARCH_STE_CFG_S2_TRANS) {
            arch_walk_config s2_cfg = arch_walk_config_from_word(ste0, ARCH_FAULT_STAGE_S2);

            if (!select_arch_stage2_table_base(stream_id, ste_pa, ste1, ste2,
                                                 true, true, m_arch_s2ttb)) {
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }
            if (m_arch_s2ttb == 0) {
                m_arch_fault_reason = ARCH_FAULT_STAGE2;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }

            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural stage-2 stream walk stream-id=0x"
                         << std::hex << stream_id << " ste-pa=0x" << ste_pa
                         << " s2ttb=0x" << m_arch_s2ttb << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural stage-2 stream walk stream-id=0x"
                      << std::hex << stream_id << " ste-pa=0x" << ste_pa
                      << " s2ttb=0x" << m_arch_s2ttb << std::dec << std::endl;
            const bool s2_ok =
                arch_descriptor_walk(stream_id, iova, m_arch_s2ttb, s2_cfg, write, pa,
                                     desc);
            if (!s2_ok) {
                apply_arch_stage2_fault_policy(ste1);
            } else {
                record_arch_ste_output_attrs(stream_id, ste1, "stage2-translation");
            }
            return s2_ok;
        }

        const bool translated_split_stage2_only =
            m_arch_translated_split_stage2_request_active &&
            m_arch_last_eats == ARCH_STE_EATS_SPLIT;
        const bool ats_treq_split_stage2_only =
            m_arch_ats_treq_split_stage2_request_active &&
            m_arch_last_eats == ARCH_STE_EATS_SPLIT;
        if ((ste0 & ARCH_STE_VALID) != 0 && ste_cfg == ARCH_STE_CFG_NESTED &&
            ((m_arch_atos_request_active &&
              m_arch_atos_type == ARCH_ATOS_ADDR_TYPE_STAGE2) ||
             translated_split_stage2_only ||
             ats_treq_split_stage2_only)) {
            arch_walk_config s2_cfg =
                arch_walk_config_from_word(ste0, ARCH_FAULT_STAGE_S2);

            if (!select_arch_stage2_table_base(stream_id, ste_pa, ste1, ste2,
                                                 true, true, m_arch_s2ttb)) {
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }
            if (m_arch_s2ttb == 0) {
                m_arch_fault_reason = ARCH_FAULT_STAGE2;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }

            const char* walk_name = translated_split_stage2_only ?
                "ATS translated split-stage stage-2-only walk" :
                (ats_treq_split_stage2_only ?
                     "ATS translation request split-stage stage-2-only walk" :
                     "ATOS stage-2-only walk");
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural " << walk_name
                         << " stream-id=0x" << std::hex << stream_id
                         << " ipa=0x" << iova << " s2ttb=0x" << m_arch_s2ttb
                         << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural " << walk_name
                      << " stream-id=0x" << std::hex << stream_id
                      << " ipa=0x" << iova << " s2ttb=0x" << m_arch_s2ttb
                      << std::dec << std::endl;
            m_arch_last_ipa = iova;
            const bool s2_only_ok =
                arch_descriptor_walk(stream_id, iova, m_arch_s2ttb, s2_cfg, write,
                                     pa, desc);
            if (!s2_only_ok) {
                apply_arch_stage2_fault_policy(ste1);
            } else {
                record_arch_ste_output_attrs(
                    stream_id, ste1,
                    translated_split_stage2_only ?
                        "ats-translated-split-stage2" :
                        (ats_treq_split_stage2_only ?
                             "ats-translation-request-split-stage2" :
                             "atos-stage2-translation"));
            }
            return s2_only_ok;
        }

        m_arch_cd_base = ste_cfg == ARCH_STE_CFG_NESTED ? 0 : (ste0 & ARCH_STE_S1CTXPTR_MASK);
        if (m_arch_cd_base == 0) {
            m_arch_cd_base = ste1 & ARCH_STE_WORD1_COMPAT_ADDR_MASK;
        }
        const bool s1_enabled = arch_ste_is_s1_enabled(ste0) ||
                                ((ste0 & ARCH_STE_VALID) != 0 && ste_cfg == ARCH_STE_CFG_NESTED);
        if (!s1_enabled || m_arch_cd_base == 0) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural stream descriptor stream-id=0x"
                         << std::hex << stream_id << " ste-pa=0x" << ste_pa << " ste=0x"
                         << ste0 << " cd-table=0x" << m_arch_cd_base << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: invalid architectural stream descriptor stream-id=0x"
                      << std::hex << stream_id << " ste-pa=0x" << ste_pa << " ste=0x"
                      << ste0 << " cd-table=0x" << m_arch_cd_base << std::dec << std::endl;
            return false;
        }

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural stream table walk stream-id=0x" << std::hex
                     << stream_id << " ste-pa=0x" << ste_pa << " ste=0x" << ste0
                     << " cd-table=0x" << m_arch_cd_base << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural stream table walk stream-id=0x" << std::hex
                  << stream_id << " ste-pa=0x" << ste_pa << " ste=0x" << ste0
                  << " cd-table=0x" << m_arch_cd_base << std::dec << std::endl;

        uint64_t cd_pa = 0;
        bool cd_bypass = false;
        arch_walk_config cd_s2_cfg {};
        const bool nested_stage2_for_cd = ste_cfg == ARCH_STE_CFG_NESTED;
        if (nested_stage2_for_cd) {
            cd_s2_cfg = arch_walk_config_from_word(ste0, ARCH_FAULT_STAGE_S2);
            m_arch_s2ttb = arch_ste_s2ttb(ste1, ste2);
        }
        if (!arch_cd_address(stream_id, ste0, ste1, cd_pa, cd_bypass,
                             nested_stage2_for_cd, m_arch_s2ttb,
                             nested_stage2_for_cd ? &cd_s2_cfg : nullptr)) {
            apply_arch_stage2_fault_policy(ste1);
            return false;
        }
        if (cd_bypass) {
            m_arch_last_cd = 0;
            m_arch_ttbr = 0;
            m_arch_last_asid = 0;
            m_arch_last_vmid = m_arch_current_vmid;
            m_arch_last_ipa = iova;
            record_arch_ste_output_attrs(stream_id, ste1, "context-bypass");
            if (ste_cfg == ARCH_STE_CFG_NESTED) {
                arch_walk_config s2_cfg = arch_walk_config_from_word(ste0, ARCH_FAULT_STAGE_S2);
                if (!select_arch_stage2_table_base(stream_id, ste_pa, ste1, ste2,
                                                     true, true, m_arch_s2ttb)) {
                    apply_arch_stage2_fault_policy(ste1);
                    return false;
                }
                if (m_arch_s2ttb == 0) {
                    m_arch_fault_reason = ARCH_FAULT_STAGE2;
                    m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                    apply_arch_stage2_fault_policy(ste1);
                    return false;
                }

                SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural nested S1DSS bypass stage-2 walk"
                             << " stream-id=0x" << std::hex << stream_id
                             << " ipa=0x" << iova << " s2ttb=0x" << m_arch_s2ttb
                             << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architectural nested S1DSS bypass stage-2 walk"
                          << " stream-id=0x" << std::hex << stream_id
                          << " ipa=0x" << iova << " s2ttb=0x" << m_arch_s2ttb
                          << std::dec << std::endl;
                const bool bypass_s2_ok =
                    arch_descriptor_walk(stream_id, iova, m_arch_s2ttb, s2_cfg, write,
                                         pa, desc);
                if (!bypass_s2_ok) {
                    apply_arch_stage2_fault_policy(ste1);
                }
                return bypass_s2_ok;
            }

            pa = iova;
            desc = 0;
            ats_fill(stream_id, iova & ~static_cast<uint64_t>(PAGE_SIZE - 1),
                     0, 0, false, 0, m_arch_last_security_state);
            m_arch_last_stage = ARCH_FAULT_STAGE_NONE;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural context descriptor bypass"
                         << " stream-id=0x" << std::hex << stream_id
                         << " iova=0x" << iova << " pa=0x" << pa << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural context descriptor bypass"
                      << " stream-id=0x" << std::hex << stream_id
                      << " iova=0x" << iova << " pa=0x" << pa << std::dec << std::endl;
            return true;
        }
        uint64_t cd_fetch_pa = cd_pa;
        if (ste_cfg == ARCH_STE_CFG_NESTED) {
            arch_walk_config s2_cfg = arch_walk_config_from_word(ste0, ARCH_FAULT_STAGE_S2);
            uint64_t cd_s2_desc = 0;

            m_arch_s2ttb = arch_ste_s2ttb(ste1, ste2);
            if (m_arch_s2ttb == 0) {
                m_arch_fault_reason = ARCH_FAULT_STAGE2;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                m_arch_fault_event_class = ARCH_EVENT_CLASS_CD;
                m_arch_last_ipa = cd_pa;
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }

            m_arch_fault_event_class = ARCH_EVENT_CLASS_CD;
            m_arch_last_ipa = cd_pa;
            if (!arch_descriptor_walk(stream_id, cd_pa, m_arch_s2ttb, s2_cfg, false,
                                      cd_fetch_pa, cd_s2_desc)) {
                desc = cd_s2_desc;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                m_arch_fault_event_class = ARCH_EVENT_CLASS_CD;
                m_arch_last_ipa = cd_pa;
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }
            if (arch_s2ptw_reject_device_fetch(stream_id, "CD fetch",
                                               cd_pa, cd_fetch_pa,
                                               cd_s2_desc,
                                               ARCH_EVENT_CLASS_CD)) {
                desc = cd_s2_desc;
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }

            m_arch_last_cd_pa = cd_fetch_pa;
            m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural nested CD fetch stage-2 walk"
                         << " stream-id=0x" << std::hex << stream_id
                         << " cd-ipa=0x" << cd_pa << " cd-pa=0x" << cd_fetch_pa
                         << " s2ttb=0x" << m_arch_s2ttb << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural nested CD fetch stage-2 walk"
                      << " stream-id=0x" << std::hex << stream_id
                      << " cd-ipa=0x" << cd_pa << " cd-pa=0x" << cd_fetch_pa
                      << " s2ttb=0x" << m_arch_s2ttb << std::dec << std::endl;
        }
        if (!read_downstream_u64(cd_fetch_pa, cd0, false, true) ||
            !read_downstream_u64(cd_fetch_pa + sizeof(cd0), cd1, false, true)) {
            set_arch_fetch_fault(ARCH_FAULT_CD_FETCH, ARCH_FAULT_STAGE_S1, cd_fetch_pa);
            return false;
        }
        uint64_t cd2 = 0;
        if (!read_downstream_u64(cd_fetch_pa + 2 * sizeof(cd0), cd2, false, true)) {
            set_arch_fetch_fault(ARCH_FAULT_CD_FETCH, ARCH_FAULT_STAGE_S1,
                                 cd_fetch_pa + 2 * sizeof(cd0));
            return false;
        }
        uint64_t cd5 = 0;
        if (arch_ste_s1mpam(ste1) &&
            !read_downstream_u64(cd_fetch_pa + ARCH_CD_MPAM_WORD_OFFSET, cd5,
                                 false, true)) {
            set_arch_fetch_fault(ARCH_FAULT_CD_FETCH, ARCH_FAULT_STAGE_S1,
                                 cd_fetch_pa + ARCH_CD_MPAM_WORD_OFFSET);
            return false;
        }
        if (arch_reject_reserved_cd(stream_id, cd_fetch_pa, cd0, cd1, cd2)) {
            return false;
        }

        m_arch_last_cd = cd0;
        m_arch_last_cd2 = cd2;
        m_arch_current_ssid = m_arch_last_cd_ssid;
        m_arch_current_ssid_valid = m_arch_selected_ssid_valid;
        m_arch_current_asid = arch_cd_asid(cd0);
        m_arch_last_asid = m_arch_current_asid;
        m_arch_last_vmid = m_arch_current_vmid;
        m_arch_ttbr = arch_cd_ttbr(cd1);
        m_arch_last_cd_nscfg0 = arch_cd_nscfg0(cd1);
        m_arch_last_had0 = arch_cd_had0(cd1);
        m_arch_last_had1 = arch_cd_had1(cd2);
        m_arch_current_cd_ha = arch_cd_ha(cd0);
        m_arch_current_cd_hd = arch_cd_hd(cd0);
        m_arch_current_cd_haft = arch_cd_haft(cd1);
        if (!arch_cd_is_valid(cd0) || m_arch_ttbr == 0) {
            m_arch_fault_reason = ARCH_FAULT_CD_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: invalid architectural context descriptor stream-id=0x"
                         << std::hex << stream_id << " cd-pa=0x" << cd_fetch_pa << " cd=0x" << cd0
                         << " ttbr=0x" << m_arch_ttbr << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: invalid architectural context descriptor stream-id=0x"
                      << std::hex << stream_id << " cd-pa=0x" << cd_fetch_pa << " cd=0x" << cd0
                      << " ttbr=0x" << m_arch_ttbr << std::dec << std::endl;
            return false;
        }

        if (arch_cd_e0pd_blocks_access(iova, cd1, cd2)) {
            m_arch_last_e0pd_fault = true;
            m_arch_fault_reason = ARCH_FAULT_PAGE_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            m_arch_last_stage = ARCH_FAULT_STAGE_S1;
            SCP_WARN(()) << "APOLLO_SMMU_TBU: CD.E0PD translation fault"
                         << " stream-id=0x" << std::hex << stream_id
                         << " cd-pa=0x" << cd_fetch_pa << " iova=0x" << iova
                         << " ttb1=" << (arch_va_selects_ttb1(iova) ? 1 : 0)
                         << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: CD.E0PD translation fault"
                      << " stream-id=0x" << std::hex << stream_id
                      << " cd-pa=0x" << cd_fetch_pa << " iova=0x" << iova
                      << " ttb1=" << (arch_va_selects_ttb1(iova) ? 1 : 0)
                      << std::dec << std::endl;
            return false;
        }

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural context descriptor walk stream-id=0x" << std::hex
                     << stream_id << " ssid=0x" << m_arch_last_cd_ssid
                     << " cd-pa=0x" << cd_fetch_pa << " cd=0x" << cd0
                     << " ttbr=0x" << m_arch_ttbr << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural context descriptor walk stream-id=0x" << std::hex
                  << stream_id << " ssid=0x" << m_arch_last_cd_ssid
                  << " cd-pa=0x" << cd_fetch_pa << " cd=0x" << cd0
                  << " ttbr=0x" << m_arch_ttbr << std::dec << std::endl;

        arch_walk_config s1_cfg = arch_walk_config_from_word(cd0, ARCH_FAULT_STAGE_S1);
        arch_walk_config nested_s2_cfg {};
        const bool nested_stage2 = ste_cfg == ARCH_STE_CFG_NESTED;
        const bool secure_stream =
            arch_security_eventq_index(m_arch_last_security_state) == ARCH_SECURITY_SECURE;
        bool s1_output_nonsecure_ipa = !secure_stream || m_arch_last_cd_nscfg0;
        uint64_t nested_secure_s2ttb = 0;
        m_arch_last_s1_table_walk_nonsecure = s1_output_nonsecure_ipa;
        m_arch_last_s1_output_nonsecure_ipa = s1_output_nonsecure_ipa;
        if (nested_stage2) {
            nested_s2_cfg = arch_walk_config_from_word(ste0, ARCH_FAULT_STAGE_S2);
            m_arch_s2ttb = arch_ste_s2ttb(ste1, ste2);
            if (m_arch_s2ttb == 0) {
                m_arch_fault_reason = ARCH_FAULT_STAGE2;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
                m_arch_fault_event_class = ARCH_EVENT_CLASS_TT;
                m_arch_last_ipa = m_arch_ttbr;
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }
            if (secure_stream && !s1_output_nonsecure_ipa &&
                !read_arch_ste_s_s2ttb(stream_id, ste_pa, nested_secure_s2ttb)) {
                apply_arch_stage2_fault_policy(ste1);
                return false;
            }
        }
        record_arch_mpam_from_cd(stream_id, cd5, nested_stage2, arch_ste_s1mpam(ste1));
        uint64_t ipa = 0;
        uint64_t s1_desc = 0;
        const bool stage1_had =
            arch_cd_had_disables_hier_attrs(iova, cd1, cd2);
        if (!arch_descriptor_walk(stream_id, iova, m_arch_ttbr, s1_cfg, write, ipa, s1_desc,
                                  nested_stage2, m_arch_s2ttb,
                                  nested_stage2 ? &nested_s2_cfg : nullptr,
                                  nested_secure_s2ttb,
                                  secure_stream, s1_output_nonsecure_ipa,
                                  &s1_output_nonsecure_ipa,
                                  stage1_had)) {
            desc = s1_desc;
            apply_arch_stage2_fault_policy(ste1);
            return false;
        }

        m_arch_last_ipa = ipa;
        if (m_arch_atos_request_active &&
            m_arch_atos_type == ARCH_ATOS_ADDR_TYPE_STAGE1) {
            pa = ipa;
            desc = s1_desc;
            record_arch_ste_output_attrs(stream_id, ste1, "atos-stage1-translation");
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural ATOS stage-1-only walk"
                         << " stream-id=0x" << std::hex << stream_id
                         << " va=0x" << iova << " ipa=0x" << ipa << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural ATOS stage-1-only walk"
                      << " stream-id=0x" << std::hex << stream_id
                      << " va=0x" << iova << " ipa=0x" << ipa << std::dec
                      << std::endl;
            return true;
        }
        if (!nested_stage2) {
            pa = ipa;
            desc = s1_desc;
            record_arch_ste_output_attrs(stream_id, ste1, "stage1-translation");
            return true;
        }

        if (!select_arch_stage2_table_base(stream_id, ste_pa, ste1, ste2,
                                           false, s1_output_nonsecure_ipa,
                                           m_arch_s2ttb)) {
            apply_arch_stage2_fault_policy(ste1);
            return false;
        }
        if (m_arch_s2ttb == 0) {
            m_arch_fault_reason = ARCH_FAULT_STAGE2;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S2;
            m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
            m_arch_last_ipa = ipa;
            apply_arch_stage2_fault_policy(ste1);
            return false;
        }

        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural nested stage-2 walk stream-id=0x"
                     << std::hex << stream_id << " ipa=0x" << ipa
                     << " nsipa=" << (s1_output_nonsecure_ipa ? 1 : 0)
                     << " s2ttb=0x" << m_arch_s2ttb
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural nested stage-2 walk stream-id=0x"
                  << std::hex << stream_id << " ipa=0x" << ipa
                  << " nsipa=" << (s1_output_nonsecure_ipa ? 1 : 0)
                  << " s2ttb=0x" << m_arch_s2ttb
                  << std::dec << std::endl;
        const bool nested_s2_ok =
            arch_descriptor_walk(stream_id, ipa, m_arch_s2ttb, nested_s2_cfg, write,
                                 pa, desc);
        if (!nested_s2_ok) {
            apply_arch_stage2_fault_policy(ste1);
        } else {
            record_arch_ste_output_attrs(stream_id, ste1, "nested-translation");
        }
        return nested_s2_ok;
    }

    bool read_arch_ste_words(uint32_t stream_id, uint64_t& ste0, uint64_t& ste1,
                             uint64_t& ste2, uint64_t& ste_pa)
    {
        if (m_arch_ste_base == 0 && !arch_active_stream_table_configured()) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            return false;
        }
        if (!arch_ste_address(stream_id, ste_pa)) {
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            return false;
        }
        if (!read_downstream_u64(ste_pa, ste0, true) ||
            !read_downstream_u64(ste_pa + sizeof(ste0), ste1, true) ||
            !read_downstream_u64(ste_pa + 2 * sizeof(ste0), ste2, true)) {
            set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1, ste_pa);
            return false;
        }
        if (arch_reject_reserved_ste(stream_id, ste_pa, ste0, ste1, ste2)) {
            return false;
        }
        m_arch_last_ste = ste0;
        m_arch_last_ste1 = ste1;
        m_arch_current_s2_ptw = arch_ste_s2ptw(ste2);
        m_arch_current_s2_ha = arch_ste_s2ha(ste2);
        m_arch_current_s2_hd = arch_ste_s2hd(ste2);
        m_arch_current_s2_haft = arch_ste_s2haft(ste2);
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        reset_arch_mpam_state();
        m_arch_last_eats = arch_effective_eats(ste0, ste1);
        return true;
    }

    void reject_arch_ats_translation_request(uint32_t stream_id, const char* reason,
                                             bool record = true)
    {
        m_arch_fault_reason = ARCH_FAULT_BAD_ATS_TREQ;
        m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
        log_arch_protocol_response(stream_id, m_arch_iova, 0, false);
        if (record) {
            record_fault(stream_id, reason, m_arch_iova, PAGE_SIZE);
        } else {
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected ATS config fault suppressed"
                         << " stream-id=0x" << std::hex << stream_id
                         << " reason=" << reason << " cr2=0x" << m_arch_cr2 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected ATS config fault suppressed"
                      << " stream-id=0x" << std::hex << stream_id
                      << " reason=" << reason << " cr2=0x" << m_arch_cr2 << std::dec
                      << std::endl;
        }
        m_arch_status = ARCH_STATUS_ERROR;
    }

    void log_arch_protocol_response_status(uint32_t stream_id, uint64_t iova,
                                           uint64_t pa, uint8_t ats_status)
    {
        const uint16_t prg = allocate_prg(stream_id, iova, ats_status);

        record_arch_ats_response(ats_status);
        push_pri_protocol_record(stream_id, prg, ats_status, iova, pa, PAGE_SIZE);
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected ATS translation response stream-id=0x" << std::hex
                     << stream_id << " iova=0x" << iova << " pa=0x" << pa
                     << " status=" << arch_ats_response_name(ats_status)
                     << " reason=" << arch_fault_reason_name(m_arch_fault_reason) << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected ATS translation response stream-id=0x" << std::hex
                  << stream_id << " iova=0x" << iova << " pa=0x" << pa
                  << " status=" << arch_ats_response_name(ats_status)
                  << " reason=" << arch_fault_reason_name(m_arch_fault_reason) << std::dec << std::endl;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected PRI response stream-id=0x" << std::hex
                     << stream_id << " prg=0x" << prg << " iova=0x" << iova
                     << " status=pending"
                     << " reason=" << arch_fault_reason_name(m_arch_fault_reason) << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected PRI response stream-id=0x" << std::hex
                  << stream_id << " prg=0x" << prg << " iova=0x" << iova
                  << " status=pending"
                  << " reason=" << arch_fault_reason_name(m_arch_fault_reason) << std::dec << std::endl;
    }

    void log_arch_protocol_response(uint32_t stream_id, uint64_t iova,
                                    uint64_t pa, bool success)
    {
        log_arch_protocol_response_status(stream_id, iova, pa,
                                          arch_ats_response_code(success));
    }

    void log_arch_ats_treq_response(uint32_t stream_id, uint64_t iova,
                                    uint64_t pa, bool success)
    {
        log_arch_protocol_response_status(stream_id, iova, pa,
                                          arch_ats_treq_response_code(success));
    }

    void run_arch_ats_translation_request(bool write = false)
    {
        uint64_t ste0 = 0;
        uint64_t ste1 = 0;
        uint64_t ste2 = 0;
        uint64_t ste_pa = 0;
        uint64_t desc = 0;
        uint64_t pa = 0;
        const uint32_t stream_id = selected_arch_stream_id();
        const uint64_t request_iova = m_arch_iova;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_walk_depth = 0;
        m_arch_last_desc = 0;
        m_arch_last_pa = 0;
        m_arch_last_ipa = 0;
        m_arch_last_fetch_addr = 0;
        m_arch_s2ttb = 0;
        m_arch_last_ste = 0;
        m_arch_last_ste1 = 0;
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_cd = 0;
        m_arch_current_asid = 0;
        m_arch_current_vmid = 0;
        m_arch_current_ssid = 0;
        m_arch_current_ssid_valid = false;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        m_arch_fault_nsipa = false;
        m_arch_fault_gpcf = false;
        m_arch_fault_record_suppressed = false;
        m_arch_last_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_last_eats = ARCH_STE_EATS_DISABLED;
        m_arch_current_access_privileged = false;
        m_arch_current_access_instruction = false;
        m_arch_last_ats_treq_write = write;
        m_arch_ats_treq_split_stage2_request_active = false;

        if (!arch_smmu_enabled()) {
            reject_arch_ats_translation_request(stream_id, "ats-smmu-disabled",
                                                arch_record_smmuen_disabled_ats_treq());
            return;
        }
        if (!read_arch_ste_words(stream_id, ste0, ste1, ste2, ste_pa)) {
            log_arch_ats_treq_response(stream_id, request_iova, 0, false);
            if (m_arch_fault_reason != ARCH_FAULT_BAD_STREAM_ID ||
                arch_record_bad_streamid_ats_treq()) {
                record_fault(stream_id, "ats-ste-fetch", request_iova, PAGE_SIZE);
            } else {
                SCP_INFO(()) << "APOLLO_SMMU_TBU: architected ATS bad StreamID suppressed"
                             << " stream-id=0x" << std::hex << stream_id
                             << " cr2=0x" << m_arch_cr2 << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architected ATS bad StreamID suppressed"
                          << " stream-id=0x" << std::hex << stream_id
                          << " cr2=0x" << m_arch_cr2 << std::dec << std::endl;
            }
            return;
        }
        if ((ste0 & ARCH_STE_VALID) != 0 &&
            arch_ste_config(ste0) == ARCH_STE_CFG_ABORT) {
            reject_arch_ats_translation_request(stream_id, "ats-config-abort");
            return;
        }
        if ((ste0 & ARCH_STE_VALID) != 0 && arch_ste_config(ste0) == 0) {
            m_arch_fault_reason = ARCH_FAULT_STREAM_DISABLED;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            m_arch_fault_record_suppressed = true;
            log_arch_ats_treq_response(stream_id, request_iova, 0, false);
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS stream disabled no-event"
                         << " stream-id=0x" << std::hex << stream_id << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: ATS stream disabled no-event"
                      << " stream-id=0x" << std::hex << stream_id << std::dec
                      << std::endl;
            return;
        }
        if (!arch_fetch_vms_if_enabled(stream_id, ste_pa, ste0, ste1)) {
            log_arch_ats_treq_response(stream_id, request_iova, 0, false);
            if (arch_record_translated_config_fault()) {
                record_fault(stream_id, "ats-vms-fetch", request_iova, PAGE_SIZE);
            } else {
                m_arch_fault_record_suppressed = true;
            }
            return;
        }
        if (m_arch_last_eats == ARCH_STE_EATS_DISABLED) {
            reject_arch_ats_translation_request(stream_id, "ats-eats-disabled");
            return;
        }
        m_arch_ats_treq_split_stage2_request_active =
            m_arch_last_eats == ARCH_STE_EATS_SPLIT &&
            arch_translated_split_stage_supported(arch_ste_config(ste0));
        const bool walk_ok =
            arch_stream_context_walk(stream_id, request_iova, pa, desc, write);
        m_arch_ats_treq_split_stage2_request_active = false;
        m_arch_iova = request_iova;
        if (!walk_ok) {
            if (arch_ats_treq_translation_fault_has_no_smmu_event(
                    m_arch_fault_reason)) {
                /* Audit marker: ATS translation request translation fault completed R==W==0. */
                m_arch_fault_record_suppressed = true;
                log_arch_ats_treq_response(stream_id, request_iova, 0, true);
                m_arch_status = ARCH_STATUS_OK;
                SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translation request"
                             << " translation fault completed R==W==0"
                             << " stream-id=0x" << std::hex << stream_id
                             << " iova=0x" << request_iova
                             << " reason="
                             << arch_fault_reason_name(m_arch_fault_reason)
                             << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: ATS translation request"
                          << " translation fault completed R==W==0"
                          << " stream-id=0x" << std::hex << stream_id
                          << " iova=0x" << request_iova
                          << " reason="
                          << arch_fault_reason_name(m_arch_fault_reason)
                          << std::dec << std::endl;
                return;
            }
            log_arch_ats_treq_response(stream_id, request_iova, 0, false);
            record_fault(stream_id, "ats-translation-request", request_iova, PAGE_SIZE);
            return;
        }

        m_arch_last_desc = desc;
        m_arch_last_pa = pa;
        m_pri_requests++;
        log_arch_ats_treq_response(stream_id, request_iova, pa, true);
        m_arch_status = ARCH_STATUS_OK;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architected ATSCHK/EATS translation request"
                     << " stream-id=0x" << std::hex << stream_id
                     << " eats=0x" << m_arch_last_eats << " pa=0x" << pa
                     << " nw=" << (write ? 0 : 1)
                     << " httu-dirty=" << (m_arch_last_httu_dirty_update ? 1 : 0)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected ATSCHK/EATS translation request"
                  << " stream-id=0x" << std::hex << stream_id
                  << " eats=0x" << m_arch_last_eats << " pa=0x" << pa
                  << " nw=" << (write ? 0 : 1)
                  << " httu-dirty=" << (m_arch_last_httu_dirty_update ? 1 : 0)
                  << std::dec
                  << std::endl;
    }

    bool allow_arch_translated_transaction(uint32_t stream_id, const char* op, uint64_t iova,
                                           uint64_t len, bool ssid_valid, uint32_t ssid,
                                           bool privileged, bool instruction)
    {
        uint64_t ste0 = 0;
        uint64_t ste1 = 0;
        uint64_t ste2 = 0;
        uint64_t ste4 = 0;
        uint64_t ste5 = 0;
        uint64_t ste_pa = 0;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_last_ste = 0;
        m_arch_last_ste1 = 0;
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_eats = ARCH_STE_EATS_DISABLED;
        m_arch_last_fetch_addr = 0;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        m_arch_fault_nsipa = false;
        m_arch_fault_gpcf = false;
        m_arch_fault_record_suppressed = false;
        m_arch_translated_split_stage2_request_active = false;
        m_arch_translated_effective_access_valid = false;
        m_arch_translated_effective_privileged = privileged;
        m_arch_translated_effective_instruction = instruction;
        m_arch_current_access_privileged = privileged;
        m_arch_current_access_instruction = instruction;
        m_arch_last_split_stage_in_privileged = privileged;
        m_arch_last_split_stage_in_instruction = instruction;
        m_arch_last_split_stage_effective_privileged = privileged;
        m_arch_last_split_stage_effective_instruction = instruction;
        reset_arch_mpam_state();
        reset_arch_output_attrs();

        const bool pasidtt = arch_pasidtt_supported();
        const bool event_ssid_valid = pasidtt && ssid_valid;
        const uint32_t event_ssid = event_ssid_valid ? ssid : 0;
        const bool event_privileged = pasidtt && privileged;
        const bool event_instruction = pasidtt && instruction;
        if (!pasidtt && (ssid_valid || privileged || instruction)) {
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated PASIDTT disabled"
                         << " stream-id=0x" << std::hex << stream_id
                         << " ssv=0 pnu=0 ind=0" << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: ATS translated PASIDTT disabled"
                      << " stream-id=0x" << std::hex << stream_id
                      << " ssv=0 pnu=0 ind=0" << std::dec << std::endl;
        }

        if (!arch_smmu_enabled()) {
            m_arch_fault_reason = ARCH_FAULT_TRANSL_FORBIDDEN;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            record_fault(stream_id, "ats-translated-smmu-disabled", iova, len, false,
                         event_ssid_valid, event_ssid, event_privileged,
                         event_instruction);
            return false;
        }

        if (!arch_translated_addr_in_oas(iova)) {
            /*
             * SMMUv3 §3.9.1.1 leaves the exact behavior for Translated
             * addresses above the implemented PA size implementation-defined.
             * QBox chooses the no-event abort behavior and reports the reason
             * through its private status registers for deterministic tests.
             */
            m_arch_fault_reason = ARCH_FAULT_ADDR_SIZE;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            m_arch_fault_record_suppressed = true;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated address-size abort"
                         << " stream-id=0x" << std::hex << stream_id
                         << " pa=0x" << iova
                         << " mask=0x" << arch_translated_addr_mask() << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: ATS translated address-size abort"
                      << " stream-id=0x" << std::hex << stream_id
                      << " pa=0x" << iova
                      << " mask=0x" << arch_translated_addr_mask() << std::dec
                      << std::endl;
            return false;
        }

        if (!arch_atschk_enabled()) {
            /*
             * SMMUv3 §3.9.1.3: when ATSCHK==0 the SMMU does not check
             * configuration structures for ATS Translated transactions, so
             * invalid StreamID/STE/CD conditions are not detected here.
             * The QBox TBU may still use its host-memory map below to route
             * the physical access into the simulation memory target.
             */
            record_arch_mpam_from_gbp(stream_id);
            m_arch_status = ARCH_STATUS_OK;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated transaction bypasses"
                         << " configuration lookup because ATSCHK is disabled"
                         << " stream-id=0x" << std::hex << stream_id << " iova=0x"
                         << iova << " len=0x" << len << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: ATS translated transaction bypasses"
                      << " configuration lookup because ATSCHK is disabled"
                      << " stream-id=0x" << std::hex << stream_id << " iova=0x"
                      << iova << " len=0x" << len << std::dec << std::endl;
            return true;
        }

        if (!read_arch_ste_words(stream_id, ste0, ste1, ste2, ste_pa)) {
            record_or_suppress_arch_translated_config_fault(
                stream_id, "ats-translated-ste-fetch", iova, len, event_ssid_valid,
                event_ssid, event_privileged, event_instruction);
            return false;
        }

        if ((ste0 & ARCH_STE_VALID) == 0) {
            m_arch_fault_reason = ARCH_FAULT_STE_INVALID;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            record_or_suppress_arch_translated_config_fault(
                stream_id, "ats-translated-bad-ste", iova, len, event_ssid_valid,
                event_ssid, event_privileged, event_instruction);
            return false;
        }

        if ((ste0 & ARCH_STE_VALID) != 0 && arch_ste_config(ste0) == 0) {
            m_arch_fault_reason = ARCH_FAULT_STREAM_DISABLED;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            m_arch_fault_record_suppressed = true;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated stream disabled no-event"
                         << " stream-id=0x" << std::hex << stream_id << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: ATS translated stream disabled no-event"
                      << " stream-id=0x" << std::hex << stream_id << std::dec
                      << std::endl;
            return false;
        }

        if ((ste0 & ARCH_STE_VALID) != 0 &&
            arch_ste_config(ste0) == ARCH_STE_CFG_ABORT) {
            m_arch_fault_reason = ARCH_FAULT_TRANSL_FORBIDDEN;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated STE.Config abort"
                         << " stream-id=0x" << std::hex << stream_id << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: ATS translated STE.Config abort"
                      << " stream-id=0x" << std::hex << stream_id << std::dec
                      << std::endl;
            record_fault(stream_id, "ats-translated-config-abort", iova, len, false,
                         event_ssid_valid, event_ssid, event_privileged,
                         event_instruction);
            return false;
        }

        if (!arch_fetch_vms_if_enabled(stream_id, ste_pa, ste0, ste1)) {
            record_or_suppress_arch_translated_config_fault(
                stream_id, "ats-translated-vms-fetch", iova, len, event_ssid_valid,
                event_ssid, event_privileged, event_instruction);
            return false;
        }

        if (m_arch_last_eats == ARCH_STE_EATS_DISABLED) {
            m_arch_fault_reason = ARCH_FAULT_TRANSL_FORBIDDEN;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            record_fault(stream_id, "ats-translated-forbidden", iova, len, false,
                         event_ssid_valid, event_ssid, event_privileged,
                         event_instruction);
            return false;
        }

        const uint32_t translated_ste_cfg = arch_ste_config(ste0);
        if (arch_translated_eats_unsupported_by_protocol(m_arch_last_eats,
                                                         translated_ste_cfg)) {
            /*
             * SMMUv3 permits implementation-defined F_TRANSL_FORBIDDEN for
             * STE.EATS==0b10 when split-stage ATS is inappropriate for the
             * transaction protocol.  QBox does not yet model a protocol that
             * carries Translated IPA traffic into the TBU, so reject it
             * deterministically instead of silently treating the IPA as a PA.
             */
            m_arch_fault_reason = ARCH_FAULT_TRANSL_FORBIDDEN;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated split-stage unsupported"
                         << " stream-id=0x" << std::hex << stream_id
                         << " eats=0x" << m_arch_last_eats << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: ATS translated split-stage unsupported"
                      << " stream-id=0x" << std::hex << stream_id
                      << " eats=0x" << m_arch_last_eats << std::dec << std::endl;
            record_fault(stream_id, "ats-translated-split-unsupported", iova, len,
                         false, event_ssid_valid, event_ssid, event_privileged,
                         event_instruction);
            return false;
        }

        if (arch_translated_eats_unsupported_by_dpt(m_arch_last_eats)) {
            /*
             * QBox does not yet model Device Permission Table lookup state.
             * Treat EATS==0b11 Translated traffic as a failed DPT check and
             * record F_TRANSL_FORBIDDEN rather than allowing an unchecked PA.
             */
            m_arch_fault_reason = ARCH_FAULT_TRANSL_FORBIDDEN;
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated DPT unsupported"
                         << " stream-id=0x" << std::hex << stream_id
                         << " eats=0x" << m_arch_last_eats << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: ATS translated DPT unsupported"
                      << " stream-id=0x" << std::hex << stream_id
                      << " eats=0x" << m_arch_last_eats << std::dec << std::endl;
            record_fault(stream_id, "ats-translated-dpt-unsupported", iova, len,
                         false, event_ssid_valid, event_ssid, event_privileged,
                         event_instruction);
            return false;
        }

        const bool translated_s1mpam_from_cd =
            translated_ste_cfg == ARCH_STE_CFG_S1_TRANS && arch_ste_s1mpam(ste1);
        if (translated_ste_cfg == ARCH_STE_CFG_S2_TRANS || !arch_ste_s1mpam(ste1) ||
            translated_s1mpam_from_cd) {
            if (!read_downstream_u64(ste_pa + ARCH_STE_MPAM_WORD4_OFFSET, ste4, true)) {
                set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1,
                                     ste_pa + ARCH_STE_MPAM_WORD4_OFFSET);
                record_or_suppress_arch_translated_config_fault(
                    stream_id, "ats-translated-ste-mpam-fetch", iova, len,
                    event_ssid_valid, event_ssid, event_privileged,
                    event_instruction);
                return false;
            }
            if (arch_ste_s1mpam(ste1) && m_arch_last_ste5 != 0) {
                ste5 = m_arch_last_ste5;
            } else if (!read_downstream_u64(ste_pa + ARCH_STE_VMSPTR_OFFSET, ste5, true)) {
                set_arch_fetch_fault(ARCH_FAULT_STE_FETCH, ARCH_FAULT_STAGE_S1,
                                     ste_pa + ARCH_STE_VMSPTR_OFFSET);
                record_or_suppress_arch_translated_config_fault(
                    stream_id, "ats-translated-ste-mpam-fetch", iova, len,
                    event_ssid_valid, event_ssid, event_privileged,
                    event_instruction);
                return false;
            }
            record_arch_mpam_from_ste(stream_id, ste4, ste5);
        }
        if (translated_s1mpam_from_cd) {
            uint64_t translated_cd_pa = 0;
            bool translated_cd_bypass = false;
            uint64_t translated_cd5 = 0;

            m_arch_cd_base = ste0 & ARCH_STE_S1CTXPTR_MASK;
            if (m_arch_cd_base == 0) {
                m_arch_cd_base = ste1 & ARCH_STE_WORD1_COMPAT_ADDR_MASK;
            }
            if (m_arch_cd_base == 0 ||
                !arch_cd_address(stream_id, ste0, ste1, translated_cd_pa,
                                 translated_cd_bypass) ||
                translated_cd_bypass) {
                m_arch_fault_reason = ARCH_FAULT_CD_INVALID;
                m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
                record_or_suppress_arch_translated_config_fault(
                    stream_id, "ats-translated-cd-mpam-fetch", iova, len,
                    event_ssid_valid, event_ssid, event_privileged,
                    event_instruction);
                return false;
            }
            if (!read_downstream_u64(translated_cd_pa + ARCH_CD_MPAM_WORD_OFFSET,
                                     translated_cd5, false, true)) {
                set_arch_fetch_fault(ARCH_FAULT_CD_FETCH, ARCH_FAULT_STAGE_S1,
                                     translated_cd_pa + ARCH_CD_MPAM_WORD_OFFSET);
                record_or_suppress_arch_translated_config_fault(
                    stream_id, "ats-translated-cd-mpam-fetch", iova, len,
                    event_ssid_valid, event_ssid, event_privileged,
                    event_instruction);
                return false;
            }
            record_arch_mpam_from_cd(stream_id, translated_cd5, false, true);
        }

        m_arch_translated_split_stage2_request_active =
            m_arch_last_eats == ARCH_STE_EATS_SPLIT &&
            arch_translated_split_stage_supported(translated_ste_cfg);
        if (m_arch_translated_split_stage2_request_active) {
            const bool write = std::strcmp(op, "write") == 0;
            m_arch_translated_effective_access_valid = true;
            m_arch_last_split_stage_in_privileged = privileged;
            m_arch_last_split_stage_in_instruction = instruction;
            m_arch_translated_effective_privileged =
                arch_ste_effective_privileged(ste1, privileged);
            m_arch_translated_effective_instruction =
                arch_ste_effective_instruction(ste1, instruction, write);
            m_arch_last_split_stage_effective_privileged =
                m_arch_translated_effective_privileged;
            m_arch_last_split_stage_effective_instruction =
                m_arch_translated_effective_instruction;
        }
        record_arch_ste_output_attrs(stream_id, ste1, "ats-translated");
        SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated transaction permitted"
                     << " stream-id=0x" << std::hex << stream_id
                     << " eats=0x" << m_arch_last_eats << " iova=0x" << iova
                     << " len=0x" << len
                     << " pnu=" << (m_arch_translated_effective_privileged ? 1 : 0)
                     << " ind=" << (m_arch_translated_effective_instruction ? 1 : 0)
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: ATS translated transaction permitted"
                  << " stream-id=0x" << std::hex << stream_id
                  << " eats=0x" << m_arch_last_eats << " iova=0x" << iova
                  << " len=0x" << len
                  << " pnu=" << (m_arch_translated_effective_privileged ? 1 : 0)
                  << " ind=" << (m_arch_translated_effective_instruction ? 1 : 0)
                  << std::dec << std::endl;
        m_arch_status = ARCH_STATUS_OK;
        return true;
    }

    void record_or_suppress_arch_translated_config_fault(uint32_t stream_id, const char* op,
                                                         uint64_t iova, uint64_t len,
                                                         bool ssid_valid, uint32_t ssid,
                                                         bool privileged,
                                                         bool instruction)
    {
        /*
         * SMMUv3 §3.9.1.3 gates configuration-structure events observed while
         * checking ATS Translated transactions with CR2.REC_CFG_ATS.  Unlike
         * ordinary C_BAD_STREAMID reporting, RECINVSID does not suppress these
         * Translated-transaction reports.
         */
        if (arch_record_translated_config_fault()) {
            record_fault(stream_id, op, iova, len, false, ssid_valid, ssid, privileged,
                         instruction);
            return;
        }

        m_arch_fault_record_suppressed = true;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: ATS translated config fault suppressed"
                     << " stream-id=0x" << std::hex << stream_id
                     << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                     << " cr2=0x" << m_arch_cr2 << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: ATS translated config fault suppressed"
                  << " stream-id=0x" << std::hex << stream_id
                  << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                  << " cr2=0x" << m_arch_cr2 << std::dec << std::endl;
    }

    void run_arch_f_uut_event()
    {
        const uint32_t stream_id = selected_arch_stream_id();

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_walk_depth = 0;
        m_arch_last_desc = 0;
        m_arch_last_pa = 0;
        m_arch_last_ipa = 0;
        m_arch_last_fetch_addr = 0;
        m_arch_s2ttb = 0;
        m_arch_last_ste = 0;
        m_arch_last_ste1 = 0;
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_cd = 0;
        m_arch_current_asid = 0;
        m_arch_current_vmid = 0;
        m_arch_current_ssid = 0;
        m_arch_current_ssid_valid = false;
        m_arch_last_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_reason = ARCH_FAULT_UNSUPPORTED_UPSTREAM;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        m_arch_fault_nsipa = false;
        m_arch_fault_gpcf = false;
        m_arch_fault_record_suppressed = false;
        m_arch_fault_stage2_stall = false;

        record_fault(stream_id, "unsupported-upstream", m_arch_iova, PAGE_SIZE);
        m_arch_status = ARCH_STATUS_OK;
    }

    void run_arch_tlb_conflict_probe()
    {
        const uint32_t stream_id = selected_arch_stream_id();
        const uint64_t request_iova = m_arch_iova;
        const uint64_t page = page_base(request_iova);
        const bool ssid_valid = m_arch_selected_ssid_valid;
        const uint32_t ssid = m_arch_selected_ssid;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_fault_record_suppressed = false;
        m_arch_fault_stage2_stall = false;
        clear_ats_cache_page(stream_id, page);
        ats_fill(stream_id, page, 0x10, 0x20, ssid_valid, ssid);
        if (!record_ats_cache_conflict_if_present(stream_id, request_iova, 0x11,
                                                  0x20, ssid_valid, ssid)) {
            return;
        }

        m_arch_status = ARCH_STATUS_OK;
    }

    void run_arch_cfg_conflict_probe()
    {
        const uint32_t stream_id = selected_arch_stream_id();
        const uint64_t request_iova = m_arch_iova;
        const uint32_t security_state = ARCH_SECURITY_NONSECURE;
        constexpr uint32_t span_log2 = 2;
        const uint64_t base_ste0 =
            ARCH_STE_VALID |
            (static_cast<uint64_t>(ARCH_STE_CFG_S1_TRANS) << ARCH_STE_CFG_SHIFT);
        const uint64_t stale_ste0 = base_ste0;
        const uint64_t stale_ste1 = 0x4000;
        const uint64_t stale_ste2 = 0;
        const uint64_t request_ste0 =
            base_ste0 | (1ULL << ARCH_START_LEVEL_SHIFT);
        const uint64_t request_ste1 = stale_ste1;
        const uint64_t request_ste2 = stale_ste2;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_fault_record_suppressed = false;
        m_arch_fault_stage2_stall = false;
        clear_config_cache_span(stream_id, security_state, span_log2);
        config_cache_fill(stream_id, security_state, stale_ste0, stale_ste1,
                          stale_ste2, span_log2);
        if (!record_config_cache_conflict_if_present(
                stream_id, request_iova, security_state, request_ste0,
                request_ste1, request_ste2, span_log2)) {
            return;
        }

        m_arch_status = ARCH_STATUS_OK;
    }

    void run_arch_probe(bool write = false)
    {
        uint64_t desc = 0;
        uint64_t pa = 0;
        const uint32_t stream_id = selected_arch_stream_id();
        const uint64_t request_iova = m_arch_iova;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_walk_depth = 0;
        m_arch_last_desc = 0;
        m_arch_last_pa = 0;
        m_arch_last_ipa = 0;
        m_arch_last_fetch_addr = 0;
        m_arch_s2ttb = 0;
        m_arch_last_ste = 0;
        m_arch_last_ste1 = 0;
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_cd = 0;
        m_arch_current_asid = 0;
        m_arch_current_vmid = 0;
        m_arch_current_ssid = 0;
        m_arch_current_ssid_valid = false;
        m_arch_last_cd_ssid = 0;
        m_arch_last_s1cdmax = 0;
        m_arch_last_s1dss = 0;
        m_arch_last_s1fmt = 0;
        m_arch_last_cd_l2 = false;
        m_arch_last_cd_bypass = false;
        m_arch_last_cd_pa = 0;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        m_arch_fault_nsipa = false;
        m_arch_fault_gpcf = false;
        m_arch_fault_record_suppressed = false;
        m_arch_fault_stage2_stall = false;
        m_arch_last_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_current_access_privileged = false;
        m_arch_current_access_instruction = false;
        reset_arch_output_attrs();
        const bool walk_ok =
            arch_stream_context_walk(stream_id, request_iova, pa, desc, write);
        m_arch_iova = request_iova;
        if (!walk_ok) {
            log_arch_protocol_response(stream_id, request_iova, 0, false);
            if (m_arch_fault_reason == ARCH_FAULT_BAD_STREAM_ID &&
                !arch_record_bad_streamid_event()) {
                m_arch_fault_record_suppressed = true;
            }
            if (m_arch_fault_record_suppressed) {
                SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural probe event suppressed"
                             << " stream-id=0x" << std::hex << stream_id
                             << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                             << std::dec;
                std::cerr << "APOLLO_SMMU_TBU: architectural probe event suppressed"
                          << " stream-id=0x" << std::hex << stream_id
                          << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                              << std::dec << std::endl;
            } else {
                record_fault(stream_id, write ? "write" : "architectural-probe",
                             request_iova, PAGE_SIZE, m_arch_fault_stage2_stall);
            }
            return;
        }

        m_arch_last_desc = desc;
        m_arch_last_pa = pa;
        m_pri_requests++;
        log_arch_protocol_response(stream_id, request_iova, pa, true);
        m_arch_status = ARCH_STATUS_OK;
    }

    void run_arch_gatos_translate(bool write = false, bool record_fault_event = true,
                                  bool record_protocol_response = true)
    {
        uint64_t desc = 0;
        uint64_t pa = 0;
        const uint32_t stream_id = selected_arch_stream_id();
        const uint64_t request_iova = m_arch_iova;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_last_par = 0;
        m_arch_walk_depth = 0;
        m_arch_last_desc = 0;
        m_arch_last_pa = 0;
        m_arch_last_ipa = 0;
        m_arch_last_fetch_addr = 0;
        m_arch_s2ttb = 0;
        m_arch_last_ste = 0;
        m_arch_last_ste1 = 0;
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_cd = 0;
        m_arch_current_asid = 0;
        m_arch_current_vmid = 0;
        m_arch_current_ssid = 0;
        m_arch_current_ssid_valid = false;
        m_arch_last_cd_ssid = 0;
        m_arch_last_s1cdmax = 0;
        m_arch_last_s1dss = 0;
        m_arch_last_s1fmt = 0;
        m_arch_last_cd_l2 = false;
        m_arch_last_cd_bypass = false;
        m_arch_last_cd_pa = 0;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        m_arch_fault_nsipa = false;
        m_arch_fault_gpcf = false;
        m_arch_fault_record_suppressed = false;
        m_arch_fault_stage2_stall = false;
        m_arch_last_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_current_access_privileged = false;
        m_arch_current_access_instruction = false;
        reset_arch_output_attrs();

        if (!arch_smmu_enabled_for_security_state(m_arch_last_security_state)) {
            m_arch_last_par = arch_gatos_par_fault(0xfd, 0, 0);
            m_arch_status = ARCH_STATUS_OK;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural GATOS fault PAR"
                         << " stream-id=0x" << std::hex << stream_id
                         << " iova=0x" << request_iova
                         << " faultcode=0xfd par=0x" << m_arch_last_par
                         << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural GATOS fault PAR"
                      << " stream-id=0x" << std::hex << stream_id
                      << " iova=0x" << request_iova
                      << " faultcode=0xfd par=0x" << m_arch_last_par
                      << std::dec << std::endl;
            return;
        }

        if (m_arch_atos_request_active &&
            (m_arch_atos_type == ARCH_ATOS_ADDR_TYPE_RESERVED ||
             (m_arch_atos_virtual_interface &&
              m_arch_atos_type != ARCH_ATOS_ADDR_TYPE_STAGE1))) {
            m_arch_fault_reason = ARCH_FAULT_ATOS_INV_REQ;
            m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
            m_arch_last_par = arch_gatos_par_fault(0xff, 0, 0);
            m_arch_status = ARCH_STATUS_OK;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural ATOS invalid request PAR"
                         << " stream-id=0x" << std::hex << stream_id
                         << " iova=0x" << request_iova
                         << " type=0x" << m_arch_atos_type
                         << " par=0x" << m_arch_last_par << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural ATOS invalid request PAR"
                      << " stream-id=0x" << std::hex << stream_id
                      << " iova=0x" << request_iova
                      << " type=0x" << m_arch_atos_type
                      << " par=0x" << m_arch_last_par << std::dec << std::endl;
            return;
        }

        const bool walk_ok =
            arch_stream_context_walk(stream_id, request_iova, pa, desc, write);
        m_arch_iova = request_iova;
        if (!walk_ok) {
            uint64_t fault_code = arch_gatos_fault_code(m_arch_fault_reason);
            if (m_arch_fault_reason == ARCH_FAULT_STREAM_DISABLED) {
                fault_code = 0xfe;
            }
            m_arch_last_par =
                arch_gatos_par_fault(fault_code, arch_gatos_fault_reason_field(),
                                     arch_gatos_fault_addr_field());
            if (record_protocol_response) {
                log_arch_protocol_response(stream_id, request_iova, 0, false);
            }
            if (!m_arch_fault_record_suppressed && record_fault_event) {
                record_fault(stream_id, write ? "gatos-write" : "gatos-translate",
                             request_iova, PAGE_SIZE);
            }
            m_arch_status = ARCH_STATUS_OK;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural GATOS fault PAR"
                         << " stream-id=0x" << std::hex << stream_id
                         << " iova=0x" << request_iova
                         << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                         << " faultcode=0x" << fault_code
                         << " par=0x" << m_arch_last_par << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural GATOS fault PAR"
                      << " stream-id=0x" << std::hex << stream_id
                      << " iova=0x" << request_iova
                      << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                      << " faultcode=0x" << fault_code
                      << " par=0x" << m_arch_last_par << std::dec << std::endl;
            return;
        }

        m_arch_last_desc = desc;
        m_arch_last_pa = pa;
        if (m_arch_last_stage == ARCH_FAULT_STAGE_NONE) {
            m_arch_last_par = arch_gatos_par_fault(0xfe, 0, 0);
            m_arch_status = ARCH_STATUS_OK;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural GATOS fault PAR"
                         << " stream-id=0x" << std::hex << stream_id
                         << " iova=0x" << request_iova
                         << " reason=invalid-stage faultcode=0xfe par=0x"
                         << m_arch_last_par << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural GATOS fault PAR"
                      << " stream-id=0x" << std::hex << stream_id
                      << " iova=0x" << request_iova
                      << " reason=invalid-stage faultcode=0xfe par=0x"
                      << m_arch_last_par << std::dec << std::endl;
            return;
        }

        m_arch_last_par = arch_gatos_success_par(pa);
        if (record_protocol_response) {
            log_arch_protocol_response(stream_id, request_iova, pa, true);
        }
        m_arch_status = ARCH_STATUS_OK;
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural GATOS translation"
                     << " stream-id=0x" << std::hex << stream_id
                     << " iova=0x" << request_iova << " pa=0x" << pa
                     << " par=0x" << m_arch_last_par << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural GATOS translation"
                  << " stream-id=0x" << std::hex << stream_id
                  << " iova=0x" << request_iova << " pa=0x" << pa
                  << " par=0x" << m_arch_last_par << std::dec << std::endl;
    }

    void run_arch_gatos_register_translate(bool secure = false)
    {
        uint32_t& gatos_ctrl = secure ? m_arch_secure_gatos_ctrl : m_arch_gatos_ctrl;
        const uint64_t gatos_sid = secure ? m_arch_secure_gatos_sid : m_arch_gatos_sid;
        const uint64_t gatos_addr = secure ? m_arch_secure_gatos_addr : m_arch_gatos_addr;
        const bool secure_stream_lookup =
            secure && ((gatos_sid & ARCH_ATOS_SID_SECURE_STREAM) != 0);
        const uint64_t saved_iova = m_arch_iova;
        const uint32_t saved_stream_id = m_arch_stream_id;
        const bool saved_stream_id_valid = m_arch_stream_id_valid;
        const uint32_t saved_ssid = m_arch_selected_ssid;
        const bool saved_ssid_valid = m_arch_selected_ssid_valid;
        const uint8_t saved_security_state = m_arch_last_security_state;
        const bool saved_security_supported = m_arch_last_security_supported;
        const bool saved_atos_request_active = m_arch_atos_request_active;
        const uint32_t saved_atos_type = m_arch_atos_type;
        const bool saved_atos_virtual = m_arch_atos_virtual_interface;
        const uint32_t atos_type =
            static_cast<uint32_t>((gatos_addr >> ARCH_ATOS_ADDR_TYPE_SHIFT) &
                                  ARCH_ATOS_ADDR_TYPE_MASK);
        const bool atos_read = (gatos_addr & ARCH_ATOS_ADDR_RNW) != 0;
        const bool atos_write = !atos_read;
        const bool atos_privileged = (gatos_addr & ARCH_ATOS_ADDR_PNU) != 0;
        const bool atos_instruction =
            atos_read && ((gatos_addr & ARCH_ATOS_ADDR_IND) != 0);

        m_arch_iova = gatos_addr & ARCH_ATOS_ADDR_ADDR_MASK;
        m_arch_stream_id = static_cast<uint32_t>(gatos_sid);
        m_arch_stream_id_valid = true;
        m_arch_selected_ssid =
            static_cast<uint32_t>((gatos_sid >> 32) & ARCH_ATOS_SID_SUBSTREAMID_MASK);
        m_arch_selected_ssid_valid = (gatos_sid & ARCH_ATOS_SID_SSID_VALID) != 0;
        m_arch_last_security_state =
            secure_stream_lookup ? ARCH_SECURITY_SECURE : ARCH_SECURITY_NONSECURE;
        m_arch_last_security_supported = true;
        m_arch_atos_request_active = true;
        m_arch_atos_type = atos_type;
        m_arch_atos_virtual_interface = false;
        m_arch_last_atos_privileged = atos_privileged;
        m_arch_last_atos_instruction = atos_instruction;
        m_arch_last_atos_read = atos_read;
        m_arch_last_atos_ste_attrs_ignored = false;

        if (secure && !secure_stream_lookup &&
            (m_arch_secure_cr0ack & ARCH_CR0_SMMUEN) == 0) {
            m_arch_fault_reason = ARCH_FAULT_NONE;
            m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
            m_arch_last_par = arch_gatos_par_fault(0xfd, 0, 0);
            m_arch_status = ARCH_STATUS_OK;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural SMMU_S_GATOS"
                         << " SSEC Non-secure stream lookup blocked by Secure SMMUEN"
                         << " stream-id=0x" << std::hex << static_cast<uint32_t>(gatos_sid)
                         << " par=0x" << m_arch_last_par << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architectural SMMU_S_GATOS"
                      << " SSEC Non-secure stream lookup blocked by Secure SMMUEN"
                      << " stream-id=0x" << std::hex << static_cast<uint32_t>(gatos_sid)
                      << " par=0x" << m_arch_last_par << std::dec << std::endl;
        } else {
            run_arch_gatos_translate(atos_write, false, false);
        }
	        gatos_ctrl &= ~ARCH_GATOS_CTRL_RUN;
	        if (secure) {
	            m_arch_secure_gatos_par = m_arch_last_par;
	        } else {
	            m_arch_gatos_par = m_arch_last_par;
	        }

        /*
         * Keep the exact markers below searchable for the superproject lane
         * and compliance checker while sharing one implementation:
         * - architectural SMMU_GATOS register translation
         * - architectural SMMU_S_GATOS register translation
         */
        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural "
                     << (secure ? "SMMU_S_GATOS" : "SMMU_GATOS")
                     << " register translation"
                     << " stream-id=0x" << std::hex << static_cast<uint32_t>(gatos_sid)
                     << " ssec=" << (secure_stream_lookup ? 1 : 0)
                     << " lookup-security=0x"
                     << static_cast<uint32_t>(m_arch_last_security_state)
                     << " pnu=" << (atos_privileged ? 1 : 0)
                     << " ind=" << (atos_instruction ? 1 : 0)
                     << " rnw=" << (atos_read ? 1 : 0)
                     << " type=0x" << atos_type
                     << " addr=0x" << gatos_addr << " par=0x" << m_arch_last_par
                     << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architectural "
                  << (secure ? "SMMU_S_GATOS" : "SMMU_GATOS")
                  << " register translation"
                  << " stream-id=0x" << std::hex << static_cast<uint32_t>(gatos_sid)
                  << " ssec=" << (secure_stream_lookup ? 1 : 0)
                  << " lookup-security=0x"
                  << static_cast<uint32_t>(m_arch_last_security_state)
                  << " pnu=" << (atos_privileged ? 1 : 0)
                  << " ind=" << (atos_instruction ? 1 : 0)
                  << " rnw=" << (atos_read ? 1 : 0)
                  << " type=0x" << atos_type
                  << " addr=0x" << gatos_addr << " par=0x" << m_arch_last_par
                  << std::dec << std::endl;

        m_arch_iova = saved_iova;
        m_arch_stream_id = saved_stream_id;
        m_arch_stream_id_valid = saved_stream_id_valid;
        m_arch_selected_ssid = saved_ssid;
        m_arch_selected_ssid_valid = saved_ssid_valid;
        m_arch_last_security_state = saved_security_state;
        m_arch_last_security_supported = saved_security_supported;
	        m_arch_atos_request_active = saved_atos_request_active;
	        m_arch_atos_type = saved_atos_type;
	        m_arch_atos_virtual_interface = saved_atos_virtual;
	    }

	    void run_arch_vatos_register_translate(bool secure = false)
	    {
	        uint32_t& vatos_ctrl = secure ? m_arch_secure_vatos_ctrl : m_arch_vatos_ctrl;
	        const uint64_t vatos_sid = secure ? m_arch_secure_vatos_sid : m_arch_vatos_sid;
	        const uint64_t vatos_addr =
	            secure ? m_arch_secure_vatos_addr : m_arch_vatos_addr;
	        const uint64_t saved_iova = m_arch_iova;
	        const uint32_t saved_stream_id = m_arch_stream_id;
	        const bool saved_stream_id_valid = m_arch_stream_id_valid;
	        const uint32_t saved_ssid = m_arch_selected_ssid;
	        const bool saved_ssid_valid = m_arch_selected_ssid_valid;
	        const uint8_t saved_security_state = m_arch_last_security_state;
	        const bool saved_security_supported = m_arch_last_security_supported;
	        const bool saved_atos_request_active = m_arch_atos_request_active;
	        const uint32_t saved_atos_type = m_arch_atos_type;
	        const bool saved_atos_virtual = m_arch_atos_virtual_interface;
	        const uint32_t atos_type =
	            static_cast<uint32_t>((vatos_addr >> ARCH_ATOS_ADDR_TYPE_SHIFT) &
	                                  ARCH_ATOS_ADDR_TYPE_MASK);
	        const bool atos_read = (vatos_addr & ARCH_ATOS_ADDR_RNW) != 0;
	        const bool atos_write = !atos_read;
	        const bool atos_privileged = (vatos_addr & ARCH_ATOS_ADDR_PNU) != 0;
	        const bool atos_instruction =
	            atos_read && ((vatos_addr & ARCH_ATOS_ADDR_IND) != 0);

	        m_arch_iova = vatos_addr & ARCH_ATOS_ADDR_ADDR_MASK;
	        m_arch_stream_id = static_cast<uint32_t>(vatos_sid);
	        m_arch_stream_id_valid = true;
	        m_arch_selected_ssid =
	            static_cast<uint32_t>((vatos_sid >> 32) & ARCH_ATOS_SID_SUBSTREAMID_MASK);
	        m_arch_selected_ssid_valid = (vatos_sid & ARCH_ATOS_SID_SSID_VALID) != 0;
	        m_arch_last_security_state =
	            secure ? ARCH_SECURITY_SECURE : ARCH_SECURITY_NONSECURE;
	        m_arch_last_security_supported = true;
	        m_arch_atos_request_active = true;
	        m_arch_atos_type = atos_type;
	        m_arch_atos_virtual_interface = true;
	        m_arch_last_atos_privileged = atos_privileged;
	        m_arch_last_atos_instruction = atos_instruction;
	        m_arch_last_atos_read = atos_read;
	        m_arch_last_atos_ste_attrs_ignored = false;

	        run_arch_gatos_translate(atos_write, false, false);
	        vatos_ctrl &= ~ARCH_GATOS_CTRL_RUN;
	        if (secure) {
	            m_arch_secure_vatos_par = m_arch_last_par;
	        } else {
	            m_arch_vatos_par = m_arch_last_par;
	        }

	        /*
	         * Internal VATOS/S_VATOS model marker: QBox keeps IDR0.VATOS clear
	         * until the platform can expose non-overlapping guest-visible VATOS
	         * pages, but unit tests can exercise the architected stage-1-only
	         * semantics through the widened component register aperture.
	         * - architectural SMMU_VATOS register translation
	         * - architectural SMMU_S_VATOS register translation
	         */
	        SCP_INFO(()) << "APOLLO_SMMU_TBU: architectural "
	                     << (secure ? "SMMU_S_VATOS" : "SMMU_VATOS")
	                     << " register translation"
	                     << " stream-id=0x" << std::hex << static_cast<uint32_t>(vatos_sid)
	                     << " stream-world=0x"
	                     << static_cast<uint32_t>(m_arch_last_security_state)
	                     << " pnu=" << (atos_privileged ? 1 : 0)
	                     << " ind=" << (atos_instruction ? 1 : 0)
	                     << " rnw=" << (atos_read ? 1 : 0)
	                     << " type=0x" << atos_type
	                     << " addr=0x" << vatos_addr << " par=0x" << m_arch_last_par
	                     << std::dec;
	        std::cerr << "APOLLO_SMMU_TBU: architectural "
	                  << (secure ? "SMMU_S_VATOS" : "SMMU_VATOS")
	                  << " register translation"
	                  << " stream-id=0x" << std::hex << static_cast<uint32_t>(vatos_sid)
	                  << " stream-world=0x"
	                  << static_cast<uint32_t>(m_arch_last_security_state)
	                  << " pnu=" << (atos_privileged ? 1 : 0)
	                  << " ind=" << (atos_instruction ? 1 : 0)
	                  << " rnw=" << (atos_read ? 1 : 0)
	                  << " type=0x" << atos_type
	                  << " addr=0x" << vatos_addr << " par=0x" << m_arch_last_par
	                  << std::dec << std::endl;

	        m_arch_iova = saved_iova;
	        m_arch_stream_id = saved_stream_id;
	        m_arch_stream_id_valid = saved_stream_id_valid;
	        m_arch_selected_ssid = saved_ssid;
	        m_arch_selected_ssid_valid = saved_ssid_valid;
	        m_arch_last_security_state = saved_security_state;
	        m_arch_last_security_supported = saved_security_supported;
	        m_arch_atos_request_active = saved_atos_request_active;
	        m_arch_atos_type = saved_atos_type;
	        m_arch_atos_virtual_interface = saved_atos_virtual;
	    }

	    void run_arch_negative_replay(bool write = false)
    {
        uint64_t desc = 0;
        uint64_t pa = 0;
        const uint32_t stream_id = selected_arch_stream_id();
        const uint64_t request_iova = m_arch_iova;

        m_arch_status = ARCH_STATUS_ERROR;
        m_arch_walk_depth = 0;
        m_arch_last_desc = 0;
        m_arch_last_pa = 0;
        m_arch_last_ipa = 0;
        m_arch_last_fetch_addr = 0;
        m_arch_s2ttb = 0;
        m_arch_last_ste = 0;
        m_arch_last_ste1 = 0;
        m_arch_last_ste5 = 0;
        m_arch_last_vms_ptr = 0;
        m_arch_last_cd = 0;
        m_arch_current_asid = 0;
        m_arch_current_vmid = 0;
        m_arch_current_ssid = 0;
        m_arch_current_ssid_valid = false;
        m_arch_last_cd_ssid = 0;
        m_arch_last_s1cdmax = 0;
        m_arch_last_s1dss = 0;
        m_arch_last_s1fmt = 0;
        m_arch_last_cd_l2 = false;
        m_arch_last_cd_bypass = false;
        m_arch_last_cd_pa = 0;
        m_arch_fault_reason = ARCH_FAULT_NONE;
        m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
        m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
        m_arch_fault_nsipa = false;
        m_arch_fault_gpcf = false;
        m_arch_fault_record_suppressed = false;
        m_arch_fault_stage2_stall = false;
        m_arch_last_stage = ARCH_FAULT_STAGE_NONE;
        reset_arch_output_attrs();

        const bool walk_ok =
            arch_stream_context_walk(stream_id, request_iova, pa, desc, write);
        m_arch_iova = request_iova;
        if (walk_ok) {
            m_arch_last_desc = desc;
            m_arch_last_pa = pa;
            m_arch_fault_reason = ARCH_FAULT_NEGATIVE_UNEXPECTED_PASS;
            m_arch_fault_stage = m_arch_last_stage;
        } else if (m_arch_fault_record_suppressed) {
            log_arch_protocol_response(stream_id, request_iova, pa, false);
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected negative replay event suppressed"
                         << " stream-id=0x" << std::hex << stream_id
                         << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                         << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected negative replay event suppressed"
                      << " stream-id=0x" << std::hex << stream_id
                      << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                      << std::dec << std::endl;
            return;
        }

        m_arch_fault_replays++;
        log_arch_protocol_response(stream_id, request_iova, pa, false);
        const bool replay_stall =
            m_arch_fault_stage != ARCH_FAULT_STAGE_S2 || m_arch_fault_stage2_stall;
        record_fault(stream_id, write ? "write" : "architected-negative-replay",
                     request_iova, PAGE_SIZE, replay_stall);
        SCP_WARN(()) << "APOLLO_SMMU_TBU: architected fault replay queued stream-id=0x" << std::hex
                     << stream_id << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                     << " detail=0x" << m_arch_last_fault_detail
                     << " replay=" << std::dec << m_arch_fault_replays << std::hex << " iova=0x"
                     << request_iova << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: architected fault replay queued stream-id=0x" << std::hex
                  << stream_id << " reason=" << arch_fault_reason_name(m_arch_fault_reason)
                  << " detail=0x" << m_arch_last_fault_detail
                  << " replay=" << std::dec << m_arch_fault_replays << std::hex << " iova=0x"
                  << request_iova << std::dec << std::endl;
    }

    void log_map(const char* op, uint32_t stream_id, uint64_t iova, uint64_t pa, uint64_t len)
    {
        SCP_INFO(()) << "APOLLO_SMMU_TBU: " << op << " stream-id=0x" << std::hex << stream_id
                     << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                     << " active=" << std::dec << map_count();
        std::cerr << "APOLLO_SMMU_TBU: " << op << " stream-id=0x" << std::hex << stream_id
                  << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len << " active=" << std::dec
                  << map_count() << std::endl;
    }

    void log_map_error(const char* op, uint32_t stream_id, uint64_t iova, uint64_t pa, uint64_t len)
    {
        SCP_ERR(()) << "APOLLO_SMMU_TBU: " << op << " failed stream-id=0x" << std::hex << stream_id
                    << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                    << " active=" << std::dec << map_count();
        std::cerr << "APOLLO_SMMU_TBU: " << op << " failed stream-id=0x" << std::hex << stream_id
                  << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len
                  << " active=" << std::dec << map_count() << std::endl;
    }

    static uint32_t read_u64_part(uint64_t value, bool high)
    {
        return high ? static_cast<uint32_t>(value >> 32) : static_cast<uint32_t>(value);
    }

    static void write_u64_part(uint64_t& target, bool high, uint32_t value)
    {
        if (high) {
            target = (target & 0xffffffffULL) | (static_cast<uint64_t>(value) << 32);
        } else {
            target = (target & 0xffffffff00000000ULL) | value;
        }
    }

    void write_msi_cfg0_part(arch_msi_config& cfg, bool high, uint32_t value)
    {
        write_u64_part(cfg.addr, high, value);
        normalize_msi_cfg(cfg);
    }

	    static bool smmuv3_secure_page_offset(uint64_t off, uint64_t& secure_off)
	    {
	        if (off >= SMMUV3_SECURE_PAGE && off < SMMUV3_SECURE_PAGE + PAGE_SIZE) {
	            secure_off = off - SMMUV3_SECURE_PAGE;
	            return true;
        }

	        return false;
	    }

	    static bool smmuv3_vatos_page_offset(uint64_t off, uint64_t& vatos_off)
	    {
	        if (off >= SMMUV3_VATOS_PAGE && off < SMMUV3_VATOS_PAGE + PAGE_SIZE) {
	            vatos_off = off - SMMUV3_VATOS_PAGE;
	            return true;
	        }

	        return false;
	    }

	    static bool smmuv3_secure_vatos_page_offset(uint64_t off, uint64_t& vatos_off)
	    {
	        if (off >= SMMUV3_S_VATOS_PAGE &&
	            off < SMMUV3_S_VATOS_PAGE + PAGE_SIZE) {
	            vatos_off = off - SMMUV3_S_VATOS_PAGE;
	            return true;
	        }

	        return false;
	    }

    arch_security_strtab_bank& secure_strtab_bank()
    {
        return m_arch_security_strtab_banks[
            arch_security_eventq_index(ARCH_SECURITY_SECURE)];
    }

    const arch_security_strtab_bank& secure_strtab_bank() const
    {
        return m_arch_security_strtab_banks[
            arch_security_eventq_index(ARCH_SECURITY_SECURE)];
    }

    arch_security_eventq_bank& secure_eventq_bank()
    {
        return m_arch_security_eventq_banks[
            arch_security_eventq_index(ARCH_SECURITY_SECURE)];
    }

    const arch_security_eventq_bank& secure_eventq_bank() const
    {
        return m_arch_security_eventq_banks[
            arch_security_eventq_index(ARCH_SECURITY_SECURE)];
    }

    void mark_secure_strtab_configured(arch_security_strtab_bank& bank)
    {
        bank.configured = true;
        bank.valid = false;
        bank.last_ste_pa = 0;
        bank.last_stream_id = 0;
        bank.lookups = 0;
    }

	    void mark_secure_eventq_configured(arch_security_eventq_bank& bank)
	    {
	        bank.queue_configured = true;
	        bank.routed_to_separate_queue = false;
	        bank.valid = false;
	    }

	    uint32_t read_smmuv3_vatos_reg(uint64_t off, bool secure) const
	    {
	        const uint32_t vatos_ctrl =
	            secure ? m_arch_secure_vatos_ctrl : m_arch_vatos_ctrl;
	        const uint64_t vatos_sid =
	            secure ? m_arch_secure_vatos_sid : m_arch_vatos_sid;
	        const uint64_t vatos_addr =
	            secure ? m_arch_secure_vatos_addr : m_arch_vatos_addr;
	        const uint64_t vatos_par =
	            secure ? m_arch_secure_vatos_par : m_arch_vatos_par;

	        switch (off) {
	        case SMMUV3_VATOS_CTRL:
	            return vatos_ctrl;
	        case SMMUV3_VATOS_SID_LO:
	            return read_u64_part(vatos_sid, false);
	        case SMMUV3_VATOS_SID_HI:
	            return read_u64_part(vatos_sid, true);
	        case SMMUV3_VATOS_ADDR_LO:
	            return read_u64_part(vatos_addr, false);
	        case SMMUV3_VATOS_ADDR_HI:
	            return read_u64_part(vatos_addr, true);
	        case SMMUV3_VATOS_PAR_LO:
	            return read_u64_part(vatos_par, false);
	        case SMMUV3_VATOS_PAR_HI:
	            return read_u64_part(vatos_par, true);
	        default:
	            return 0;
	        }
	    }

	    void write_smmuv3_vatos_reg(uint64_t off, uint32_t value, bool secure)
	    {
	        uint32_t& vatos_ctrl =
	            secure ? m_arch_secure_vatos_ctrl : m_arch_vatos_ctrl;
	        uint64_t& vatos_sid =
	            secure ? m_arch_secure_vatos_sid : m_arch_vatos_sid;
	        uint64_t& vatos_addr =
	            secure ? m_arch_secure_vatos_addr : m_arch_vatos_addr;

	        switch (off) {
	        case SMMUV3_VATOS_CTRL:
	            vatos_ctrl = value & ARCH_GATOS_CTRL_RUN;
	            if ((vatos_ctrl & ARCH_GATOS_CTRL_RUN) != 0) {
	                run_arch_vatos_register_translate(secure);
	            }
	            break;
	        case SMMUV3_VATOS_SID_LO:
	            write_u64_part(vatos_sid, false, value);
	            break;
	        case SMMUV3_VATOS_SID_HI:
	            write_u64_part(vatos_sid, true, value);
	            break;
	        case SMMUV3_VATOS_ADDR_LO:
	            write_u64_part(vatos_addr, false, value);
	            break;
	        case SMMUV3_VATOS_ADDR_HI:
	            write_u64_part(vatos_addr, true, value);
	            break;
	        case SMMUV3_VATOS_PAR_LO:
	        case SMMUV3_VATOS_PAR_HI:
	            break;
	        default:
	            break;
	        }
	    }

	    uint32_t read_smmuv3_secure_reg(uint64_t off) const
    {
        const auto& strtab = secure_strtab_bank();
        const auto& eventq_bank = secure_eventq_bank();
        const auto& eventq = eventq_bank.queue;

        switch (off) {
        case SMMUV3_IDR0:
            return ARCH_S_IDR0;
        case SMMUV3_IDR1:
            return ARCH_S_IDR1;
        case SMMUV3_IDR2:
            return ARCH_IDR2;
        case SMMUV3_IDR3:
            return ARCH_S_IDR3;
        case SMMUV3_IDR4:
            return ARCH_S_IDR4;
        case SMMUV3_IDR5:
            return ARCH_IDR5;
        case SMMUV3_IDR6:
            /*
             * Secure ECMDQ is not advertised through SMMU_S_IDR0.ECMDQ, so
             * SMMU_S_IDR6 is RES0.
             */
            return ARCH_IDR6;
        case SMMUV3_IIDR:
            return ARCH_IIDR;
        case SMMUV3_AIDR:
            return ARCH_AIDR;
        case SMMUV3_CR0:
            return m_arch_secure_cr0;
        case SMMUV3_CR0ACK:
            return m_arch_secure_cr0ack;
        case SMMUV3_CR1:
            return m_arch_secure_cr1;
        case SMMUV3_CR2:
            return m_arch_secure_cr2;
        case SMMUV3_STATUSR:
            return m_arch_irq_status;
        case SMMUV3_GBPA:
            return m_arch_secure_gbpa;
        case SMMUV3_AGBPA:
            /*
             * SMMU_(S_)AGBPA is implementation-defined.  This Apollo model does
             * not implement extra bypass tags, so the register is RES0 as
             * permitted by the architecture for unsupported AGBPA.
             */
            return ARCH_AGBPA_UNSUPPORTED_RES0;
        case SMMUV3_IRQ_CTRL:
            return m_arch_secure_irq_ctrl;
        case SMMUV3_IRQ_CTRLACK:
            return m_arch_secure_irq_ctrlack;
        case SMMUV3_GERROR:
            return read_secure_gerror_raw();
        case SMMUV3_GERRORN:
            return m_arch_secure_gerrorn;
        case SMMUV3_GERROR_IRQ_CFG0:
            return read_u64_part(m_arch_secure_gerror_msi.addr, false);
        case SMMUV3_GERROR_IRQ_CFG0_HI:
            return read_u64_part(m_arch_secure_gerror_msi.addr, true);
        case SMMUV3_GERROR_IRQ_CFG1:
            return m_arch_secure_gerror_msi.data;
        case SMMUV3_GERROR_IRQ_CFG2:
            return m_arch_secure_gerror_msi.attr;
        case SMMUV3_STRTAB_BASE_LO:
            return read_u64_part(strtab.base, false);
        case SMMUV3_STRTAB_BASE_HI:
            return read_u64_part(strtab.base, true);
        case SMMUV3_STRTAB_BASE_CFG:
            return strtab.cfg;
        case SMMUV3_CMDQ_BASE_LO:
            return read_u64_part(m_arch_secure_cmdq.base, false);
        case SMMUV3_CMDQ_BASE_HI:
            return read_u64_part(m_arch_secure_cmdq.base, true);
        case SMMUV3_CMDQ_PROD:
            return m_arch_secure_cmdq.prod;
        case SMMUV3_CMDQ_CONS:
            return read_secure_cmdq_cons();
        case SMMUV3_EVENTQ_BASE_LO:
            return read_u64_part(eventq.base, false);
        case SMMUV3_EVENTQ_BASE_HI:
            return read_u64_part(eventq.base, true);
        case SMMUV3_EVENTQ_PROD:
            return queue_prod_reg(eventq);
        case SMMUV3_EVENTQ_CONS:
            return queue_cons_reg(eventq);
        case SMMUV3_EVENTQ_IRQ_CFG0:
            return read_u64_part(m_arch_secure_eventq_msi.addr, false);
        case SMMUV3_EVENTQ_IRQ_CFG0_HI:
            return read_u64_part(m_arch_secure_eventq_msi.addr, true);
        case SMMUV3_EVENTQ_IRQ_CFG1:
            return m_arch_secure_eventq_msi.data;
        case SMMUV3_EVENTQ_IRQ_CFG2:
            return m_arch_secure_eventq_msi.attr;
        case SMMUV3_PRIQ_BASE_LO:
            return read_u64_part(m_arch_secure_priq.base, false);
        case SMMUV3_PRIQ_BASE_HI:
            return read_u64_part(m_arch_secure_priq.base, true);
        case SMMUV3_PRIQ_PROD:
            return queue_prod_reg(m_arch_secure_priq);
        case SMMUV3_PRIQ_CONS:
            return queue_cons_reg(m_arch_secure_priq);
        case SMMUV3_PRIQ_IRQ_CFG0:
            return read_u64_part(m_arch_secure_priq_msi.addr, false);
        case SMMUV3_PRIQ_IRQ_CFG0_HI:
            return read_u64_part(m_arch_secure_priq_msi.addr, true);
        case SMMUV3_PRIQ_IRQ_CFG1:
            return m_arch_secure_priq_msi.data;
        case SMMUV3_PRIQ_IRQ_CFG2:
            return m_arch_secure_priq_msi.attr;
        case SMMUV3_GATOS_CTRL:
            return m_arch_secure_gatos_ctrl;
        case SMMUV3_GATOS_SID_LO:
            return read_u64_part(m_arch_secure_gatos_sid, false);
        case SMMUV3_GATOS_SID_HI:
            return read_u64_part(m_arch_secure_gatos_sid, true);
        case SMMUV3_GATOS_ADDR_LO:
            return read_u64_part(m_arch_secure_gatos_addr, false);
        case SMMUV3_GATOS_ADDR_HI:
            return read_u64_part(m_arch_secure_gatos_addr, true);
        case SMMUV3_GATOS_PAR_LO:
            return read_u64_part(m_arch_secure_gatos_par, false);
        case SMMUV3_GATOS_PAR_HI:
            return read_u64_part(m_arch_secure_gatos_par, true);
        case SMMUV3_MPAMIDR:
            return ARCH_MPAMIDR;
        case SMMUV3_GMPAM:
            return m_arch_secure_gmpam;
        case SMMUV3_GBPMPAM:
            return m_arch_secure_gbpmpam;
        case SMMUV3_VATOS_SEL:
            return (ARCH_IDR0 & ARCH_IDR0_VATOS) != 0 ? m_arch_secure_vatos_sel : 0;
        case SMMUV3_DPT_BASE_LO:
        case SMMUV3_DPT_BASE_HI:
        case SMMUV3_DPT_BASE_CFG:
        case SMMUV3_DPT_CFG_FAR_LO:
        case SMMUV3_DPT_CFG_FAR_HI:
            /*
             * This model advertises IDR3.DPT=0.  DPT registers are therefore
             * unsupported and exposed as RES0/RAZ-WI.
             */
            return ARCH_DPT_UNSUPPORTED_RES0;
        case SMMUV3_CMDQ_CONTROL_PAGE_BASE_LO:
        case SMMUV3_CMDQ_CONTROL_PAGE_BASE_HI:
        case SMMUV3_CMDQ_CONTROL_PAGE_CFG:
        case SMMUV3_CMDQ_CONTROL_PAGE_STATUS:
            /*
             * SMMU_S_IDR0.ECMDQ=0: Secure ECMDQ discovery/control registers
             * are architecturally RES0/RAZ-WI.
             */
            return ARCH_ECMDQ_UNSUPPORTED_RES0;
        case SMMUV3_STATUS:
            return ARCH_STATUS_READY | ARCH_STATUS_QUEUE_MODEL;
	        default:
            return 0;
        }
    }

    void write_smmuv3_secure_reg(uint64_t off, uint32_t value)
    {
        auto& strtab = secure_strtab_bank();
        auto& eventq_bank = secure_eventq_bank();

        switch (off) {
        case SMMUV3_CR0:
        {
            const bool smmuen_falling =
                (m_arch_secure_cr0 & ARCH_CR0_SMMUEN) && !(value & ARCH_CR0_SMMUEN);
            m_arch_secure_cr0 = value;
            m_arch_secure_cr0ack = value;
            if (smmuen_falling) {
                clear_stall_records();
            }
            break;
        }
        case SMMUV3_CR1:
            m_arch_secure_cr1 = value;
            break;
        case SMMUV3_CR2:
            m_arch_secure_cr2 = value & ARCH_CR2_WRITABLE_MASK;
            break;
        case SMMUV3_GBPA:
            m_arch_secure_gbpa = arch_gbpa_normalize(value);
            break;
        case SMMUV3_AGBPA:
            /* Unsupported implementation-defined AGBPA field: RES0/WI. */
            break;
        case SMMUV3_GMPAM:
            write_arch_mpam_update_reg(m_arch_secure_gmpam, value);
            break;
	        case SMMUV3_GBPMPAM:
	            write_arch_mpam_update_reg(m_arch_secure_gbpmpam, value);
	            break;
        case SMMUV3_VATOS_SEL:
            if ((ARCH_IDR0 & ARCH_IDR0_VATOS) != 0) {
                m_arch_secure_vatos_sel = value & 0x3ffu;
            }
            break;
        case SMMUV3_DPT_BASE_LO:
        case SMMUV3_DPT_BASE_HI:
        case SMMUV3_DPT_BASE_CFG:
        case SMMUV3_DPT_CFG_FAR_LO:
        case SMMUV3_DPT_CFG_FAR_HI:
            /* IDR3.DPT=0: unsupported DPT registers are RES0/WI. */
            break;
        case SMMUV3_IDR6:
        case SMMUV3_CMDQ_CONTROL_PAGE_BASE_LO:
        case SMMUV3_CMDQ_CONTROL_PAGE_BASE_HI:
        case SMMUV3_CMDQ_CONTROL_PAGE_CFG:
        case SMMUV3_CMDQ_CONTROL_PAGE_STATUS:
            /* SMMU_S_IDR0.ECMDQ=0: unsupported Secure ECMDQ registers are RES0/WI. */
            break;
        case SMMUV3_IRQ_CTRL:
            m_arch_secure_irq_ctrl = value & ARCH_IRQ_CTRL_WRITABLE_MASK;
            m_arch_secure_irq_ctrlack = m_arch_secure_irq_ctrl;
            update_irq_outputs();
            break;
        case SMMUV3_GERRORN:
            ack_secure_gerror(value);
            break;
        case SMMUV3_GERROR_IRQ_CFG0:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_GERROR)) {
                write_msi_cfg0_part(m_arch_secure_gerror_msi, false, value);
            }
            break;
        case SMMUV3_GERROR_IRQ_CFG0_HI:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_GERROR)) {
                write_msi_cfg0_part(m_arch_secure_gerror_msi, true, value);
            }
            break;
        case SMMUV3_GERROR_IRQ_CFG1:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_GERROR)) {
                m_arch_secure_gerror_msi.data = value;
            }
            break;
        case SMMUV3_GERROR_IRQ_CFG2:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_GERROR)) {
                m_arch_secure_gerror_msi.attr = value & ARCH_MSI_CFG2_WRITABLE_MASK;
            }
            break;
        case SMMUV3_STRTAB_BASE_LO:
            write_u64_part(strtab.base, false, value);
            mark_secure_strtab_configured(strtab);
            break;
        case SMMUV3_STRTAB_BASE_HI:
            write_u64_part(strtab.base, true, value);
            mark_secure_strtab_configured(strtab);
            break;
        case SMMUV3_STRTAB_BASE_CFG:
            strtab.cfg = value;
            mark_secure_strtab_configured(strtab);
            break;
        case SMMUV3_CMDQ_BASE_LO:
            write_u64_part(m_arch_secure_cmdq.base, false, value);
            break;
        case SMMUV3_CMDQ_BASE_HI:
            write_u64_part(m_arch_secure_cmdq.base, true, value);
            break;
        case SMMUV3_CMDQ_PROD:
            m_arch_secure_cmdq.prod = value & ARCH_QUEUE_INDEX_MASK;
            process_secure_cmdq();
            break;
        case SMMUV3_CMDQ_CONS:
            write_secure_cmdq_cons(value);
            break;
        case SMMUV3_EVENTQ_BASE_LO:
            write_u64_part(eventq_bank.queue.base, false, value);
            mark_secure_eventq_configured(eventq_bank);
            break;
        case SMMUV3_EVENTQ_BASE_HI:
            write_u64_part(eventq_bank.queue.base, true, value);
            mark_secure_eventq_configured(eventq_bank);
            break;
        case SMMUV3_EVENTQ_PROD:
            eventq_bank.queue.prod = value & ARCH_QUEUE_INDEX_MASK;
            mark_secure_eventq_configured(eventq_bank);
            break;
        case SMMUV3_EVENTQ_CONS:
            write_arch_output_queue_cons(eventq_bank.queue, value);
            eventq_bank.last_cons = eventq_bank.queue.cons;
            drain_stall_event_buffer();
            if (eventq_bank.queue.cons == eventq_bank.queue.prod) {
                clear_arch_irq_status(ARCH_IRQ_EVENTQ);
            }
            break;
        case SMMUV3_EVENTQ_IRQ_CFG0:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_EVENTQ)) {
                write_msi_cfg0_part(m_arch_secure_eventq_msi, false, value);
            }
            break;
        case SMMUV3_EVENTQ_IRQ_CFG0_HI:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_EVENTQ)) {
                write_msi_cfg0_part(m_arch_secure_eventq_msi, true, value);
            }
            break;
        case SMMUV3_EVENTQ_IRQ_CFG1:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_EVENTQ)) {
                m_arch_secure_eventq_msi.data = value;
            }
            break;
        case SMMUV3_EVENTQ_IRQ_CFG2:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_EVENTQ)) {
                m_arch_secure_eventq_msi.attr = value & ARCH_MSI_CFG2_WRITABLE_MASK;
            }
            break;
        case SMMUV3_PRIQ_BASE_LO:
            write_u64_part(m_arch_secure_priq.base, false, value);
            break;
        case SMMUV3_PRIQ_BASE_HI:
            write_u64_part(m_arch_secure_priq.base, true, value);
            break;
        case SMMUV3_PRIQ_PROD:
            m_arch_secure_priq.prod = value & ARCH_QUEUE_INDEX_MASK;
            break;
        case SMMUV3_PRIQ_CONS:
            write_arch_output_queue_cons(m_arch_secure_priq, value);
            if (m_arch_secure_priq.cons == m_arch_secure_priq.prod) {
                clear_arch_irq_status(ARCH_IRQ_PRIQ);
            }
            break;
        case SMMUV3_PRIQ_IRQ_CFG0:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_PRIQ)) {
                write_msi_cfg0_part(m_arch_secure_priq_msi, false, value);
            }
            break;
        case SMMUV3_PRIQ_IRQ_CFG0_HI:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_PRIQ)) {
                write_msi_cfg0_part(m_arch_secure_priq_msi, true, value);
            }
            break;
        case SMMUV3_PRIQ_IRQ_CFG1:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_PRIQ)) {
                m_arch_secure_priq_msi.data = value;
            }
            break;
        case SMMUV3_PRIQ_IRQ_CFG2:
            if (arch_secure_irq_cfg_writable(ARCH_IRQ_PRIQ)) {
                m_arch_secure_priq_msi.attr = value & ARCH_MSI_CFG2_WRITABLE_MASK;
            }
            break;
        case SMMUV3_GATOS_CTRL:
            m_arch_secure_gatos_ctrl = value & ARCH_GATOS_CTRL_RUN;
            if ((m_arch_secure_gatos_ctrl & ARCH_GATOS_CTRL_RUN) != 0) {
                run_arch_gatos_register_translate(true);
            }
            break;
        case SMMUV3_GATOS_SID_LO:
            write_u64_part(m_arch_secure_gatos_sid, false, value);
            break;
        case SMMUV3_GATOS_SID_HI:
            write_u64_part(m_arch_secure_gatos_sid, true, value);
            break;
        case SMMUV3_GATOS_ADDR_LO:
            write_u64_part(m_arch_secure_gatos_addr, false, value);
            break;
        case SMMUV3_GATOS_ADDR_HI:
            write_u64_part(m_arch_secure_gatos_addr, true, value);
            break;
        case SMMUV3_GATOS_PAR_LO:
        case SMMUV3_GATOS_PAR_HI:
            break;
        default:
            break;
        }
    }

	    uint32_t read_smmuv3_reg(uint64_t addr) const
	    {
	        const uint64_t off = addr - REG_SMMUV3_BASE;
	        uint64_t secure_off = 0;
	        uint64_t vatos_off = 0;

	        if (smmuv3_secure_page_offset(off, secure_off)) {
	            return read_smmuv3_secure_reg(secure_off);
	        }
	        if (smmuv3_vatos_page_offset(off, vatos_off)) {
	            return read_smmuv3_vatos_reg(vatos_off, false);
	        }
	        if (smmuv3_secure_vatos_page_offset(off, vatos_off)) {
	            return read_smmuv3_vatos_reg(vatos_off, true);
	        }

	        switch (off) {
        case SMMUV3_IDR0:
            return ARCH_IDR0;
        case SMMUV3_IDR1:
            return ARCH_IDR1;
        case SMMUV3_IDR2:
            return ARCH_IDR2;
        case SMMUV3_IDR3:
            return ARCH_IDR3;
        case SMMUV3_IDR4:
            return ARCH_IDR4;
        case SMMUV3_IDR5:
            return ARCH_IDR5;
        case SMMUV3_IDR6:
            /*
             * SMMU_IDR1.ECMDQ=0, so SMMU_IDR6 is RES0 and no Enhanced
             * Command queue control pages are present.
             */
            return ARCH_IDR6;
        case SMMUV3_IIDR:
            return ARCH_IIDR;
        case SMMUV3_AIDR:
            return ARCH_AIDR;
        case SMMUV3_CR0:
            return m_arch_cr0;
        case SMMUV3_CR0ACK:
            return m_arch_cr0ack;
        case SMMUV3_CR1:
            return m_arch_cr1;
        case SMMUV3_CR2:
            return m_arch_cr2;
        case SMMUV3_STATUSR:
            return m_arch_irq_status;
        case SMMUV3_GBPA:
            return m_arch_gbpa;
        case SMMUV3_AGBPA:
            /*
             * SMMU_AGBPA is implementation-defined.  This Apollo model does
             * not implement extra bypass tags, so the register is RES0 as
             * permitted by the architecture for unsupported AGBPA.
             */
            return ARCH_AGBPA_UNSUPPORTED_RES0;
        case SMMUV3_IRQ_CTRL:
            return m_arch_irq_ctrl;
        case SMMUV3_IRQ_CTRLACK:
            return m_arch_irq_ctrlack;
        case SMMUV3_GERROR:
            return read_arch_gerror_raw();
        case SMMUV3_GERRORN:
            return m_arch_gerrorn;
        case SMMUV3_GERROR_IRQ_CFG0:
            return read_u64_part(m_arch_gerror_msi.addr, false);
        case SMMUV3_GERROR_IRQ_CFG0_HI:
            return read_u64_part(m_arch_gerror_msi.addr, true);
        case SMMUV3_GERROR_IRQ_CFG1:
            return m_arch_gerror_msi.data;
        case SMMUV3_GERROR_IRQ_CFG2:
            return m_arch_gerror_msi.attr;
        case SMMUV3_STRTAB_BASE_LO:
            return read_u64_part(m_arch_strtab_base, false);
        case SMMUV3_STRTAB_BASE_HI:
            return read_u64_part(m_arch_strtab_base, true);
        case SMMUV3_STRTAB_BASE_CFG:
            return m_arch_strtab_cfg;
        case SMMUV3_CMDQ_BASE_LO:
            return read_u64_part(m_cmdq.base, false);
        case SMMUV3_CMDQ_BASE_HI:
            return read_u64_part(m_cmdq.base, true);
        case SMMUV3_CMDQ_PROD:
            return m_cmdq.prod;
        case SMMUV3_CMDQ_CONS:
            return read_cmdq_cons();
        case SMMUV3_EVENTQ_BASE_LO:
            return read_u64_part(m_eventq.base, false);
        case SMMUV3_EVENTQ_BASE_HI:
            return read_u64_part(m_eventq.base, true);
        case SMMUV3_EVENTQ_PROD:
            return queue_prod_reg(m_eventq);
        case SMMUV3_EVENTQ_CONS:
            return queue_cons_reg(m_eventq);
        case SMMUV3_EVENTQ_IRQ_CFG0:
            return read_u64_part(m_arch_eventq_msi.addr, false);
        case SMMUV3_EVENTQ_IRQ_CFG0_HI:
            return read_u64_part(m_arch_eventq_msi.addr, true);
        case SMMUV3_EVENTQ_IRQ_CFG1:
            return m_arch_eventq_msi.data;
        case SMMUV3_EVENTQ_IRQ_CFG2:
            return m_arch_eventq_msi.attr;
        case SMMUV3_PRIQ_BASE_LO:
            return read_u64_part(m_priq.base, false);
        case SMMUV3_PRIQ_BASE_HI:
            return read_u64_part(m_priq.base, true);
        case SMMUV3_PRIQ_PROD:
            return queue_prod_reg(m_priq);
        case SMMUV3_PRIQ_CONS:
            return queue_cons_reg(m_priq);
        case SMMUV3_PRIQ_IRQ_CFG0:
            return read_u64_part(m_arch_priq_msi.addr, false);
        case SMMUV3_PRIQ_IRQ_CFG0_HI:
            return read_u64_part(m_arch_priq_msi.addr, true);
        case SMMUV3_PRIQ_IRQ_CFG1:
            return m_arch_priq_msi.data;
        case SMMUV3_PRIQ_IRQ_CFG2:
            return m_arch_priq_msi.attr;
        case SMMUV3_GATOS_CTRL:
            return m_arch_gatos_ctrl;
        case SMMUV3_GATOS_SID_LO:
            return read_u64_part(m_arch_gatos_sid, false);
        case SMMUV3_GATOS_SID_HI:
            return read_u64_part(m_arch_gatos_sid, true);
        case SMMUV3_GATOS_ADDR_LO:
            return read_u64_part(m_arch_gatos_addr, false);
        case SMMUV3_GATOS_ADDR_HI:
            return read_u64_part(m_arch_gatos_addr, true);
	        case SMMUV3_GATOS_PAR_LO:
	            return read_u64_part(m_arch_gatos_par, false);
	        case SMMUV3_GATOS_PAR_HI:
	            return read_u64_part(m_arch_gatos_par, true);
        case SMMUV3_MPAMIDR:
            return ARCH_MPAMIDR;
        case SMMUV3_GMPAM:
            return m_arch_gmpam;
        case SMMUV3_GBPMPAM:
            return m_arch_gbpmpam;
        case SMMUV3_VATOS_SEL:
            return (ARCH_IDR0 & ARCH_IDR0_VATOS) != 0 ? m_arch_vatos_sel : 0;
        case SMMUV3_DPT_BASE_LO:
        case SMMUV3_DPT_BASE_HI:
        case SMMUV3_DPT_BASE_CFG:
        case SMMUV3_DPT_CFG_FAR_LO:
        case SMMUV3_DPT_CFG_FAR_HI:
            /*
             * This model advertises IDR3.DPT=0.  DPT registers are therefore
             * unsupported and exposed as RES0/RAZ-WI.
             */
            return ARCH_DPT_UNSUPPORTED_RES0;
        case SMMUV3_CMDQ_CONTROL_PAGE_BASE_LO:
        case SMMUV3_CMDQ_CONTROL_PAGE_BASE_HI:
        case SMMUV3_CMDQ_CONTROL_PAGE_CFG:
        case SMMUV3_CMDQ_CONTROL_PAGE_STATUS:
        case SMMUV3_S_CMDQ_CONTROL_PAGE_BASE_LO:
        case SMMUV3_S_CMDQ_CONTROL_PAGE_BASE_HI:
        case SMMUV3_S_CMDQ_CONTROL_PAGE_CFG:
        case SMMUV3_S_CMDQ_CONTROL_PAGE_STATUS:
            /*
             * SMMU_IDR1.ECMDQ=0 and SMMU_S_IDR0.ECMDQ=0: ECMDQ
             * discovery/control page registers are RES0/RAZ-WI.
             */
            return ARCH_ECMDQ_UNSUPPORTED_RES0;
        case SMMUV3_STATUS:
            return ARCH_STATUS_READY | ARCH_STATUS_QUEUE_MODEL;
        default:
            return 0;
        }
    }

	    void write_smmuv3_reg(uint64_t addr, uint32_t value)
	    {
	        const uint64_t off = addr - REG_SMMUV3_BASE;
	        uint64_t secure_off = 0;
	        uint64_t vatos_off = 0;

	        if (smmuv3_secure_page_offset(off, secure_off)) {
	            write_smmuv3_secure_reg(secure_off, value);
	            return;
	        }
	        if (smmuv3_vatos_page_offset(off, vatos_off)) {
	            write_smmuv3_vatos_reg(vatos_off, value, false);
	            return;
	        }
	        if (smmuv3_secure_vatos_page_offset(off, vatos_off)) {
	            write_smmuv3_vatos_reg(vatos_off, value, true);
	            return;
	        }

        switch (off) {
        case SMMUV3_CR0:
        {
            const bool smmuen_falling =
                (m_arch_cr0 & ARCH_CR0_SMMUEN) && !(value & ARCH_CR0_SMMUEN);
            m_arch_cr0 = value;
            m_arch_cr0ack = value;
            if (smmuen_falling) {
                clear_stall_records();
            }
            log_arch_queue("REG", "cr0-ack", m_arch_cr0, m_arch_cr0ack);
            break;
        }
        case SMMUV3_CR1:
            m_arch_cr1 = value;
            break;
        case SMMUV3_CR2:
            m_arch_cr2 = value & ARCH_CR2_WRITABLE_MASK;
            SCP_INFO(()) << "APOLLO_SMMU_TBU: architected REG cr2=0x" << std::hex
                         << m_arch_cr2 << std::dec;
            std::cerr << "APOLLO_SMMU_TBU: architected REG cr2=0x" << std::hex
                      << m_arch_cr2 << std::dec << std::endl;
            break;
        case SMMUV3_GBPA:
            m_arch_gbpa = arch_gbpa_normalize(value);
            break;
        case SMMUV3_AGBPA:
            /* Unsupported implementation-defined AGBPA field: RES0/WI. */
            break;
        case SMMUV3_GMPAM:
            write_arch_mpam_update_reg(m_arch_gmpam, value);
            break;
	        case SMMUV3_GBPMPAM:
	            write_arch_mpam_update_reg(m_arch_gbpmpam, value);
	            break;
        case SMMUV3_VATOS_SEL:
            if ((ARCH_IDR0 & ARCH_IDR0_VATOS) != 0) {
                m_arch_vatos_sel = value & 0x3ffu;
            }
            break;
        case SMMUV3_DPT_BASE_LO:
        case SMMUV3_DPT_BASE_HI:
        case SMMUV3_DPT_BASE_CFG:
        case SMMUV3_DPT_CFG_FAR_LO:
        case SMMUV3_DPT_CFG_FAR_HI:
            /* IDR3.DPT=0: unsupported DPT registers are RES0/WI. */
            break;
        case SMMUV3_IDR6:
        case SMMUV3_CMDQ_CONTROL_PAGE_BASE_LO:
        case SMMUV3_CMDQ_CONTROL_PAGE_BASE_HI:
        case SMMUV3_CMDQ_CONTROL_PAGE_CFG:
        case SMMUV3_CMDQ_CONTROL_PAGE_STATUS:
        case SMMUV3_S_CMDQ_CONTROL_PAGE_BASE_LO:
        case SMMUV3_S_CMDQ_CONTROL_PAGE_BASE_HI:
        case SMMUV3_S_CMDQ_CONTROL_PAGE_CFG:
        case SMMUV3_S_CMDQ_CONTROL_PAGE_STATUS:
            /* IDR1.ECMDQ=0/S_IDR0.ECMDQ=0: unsupported ECMDQ registers are RES0/WI. */
            break;
        case SMMUV3_IRQ_CTRL:
            m_arch_irq_ctrl = value & ARCH_IRQ_CTRL_WRITABLE_MASK;
            m_arch_irq_ctrlack = m_arch_irq_ctrl;
            update_irq_outputs();
            break;
        case SMMUV3_GERRORN:
            ack_arch_gerror(value);
            break;
        case SMMUV3_GERROR_IRQ_CFG0:
            if (arch_irq_cfg_writable(ARCH_IRQ_GERROR)) {
                write_msi_cfg0_part(m_arch_gerror_msi, false, value);
            }
            break;
        case SMMUV3_GERROR_IRQ_CFG0_HI:
            if (arch_irq_cfg_writable(ARCH_IRQ_GERROR)) {
                write_msi_cfg0_part(m_arch_gerror_msi, true, value);
            }
            break;
        case SMMUV3_GERROR_IRQ_CFG1:
            if (arch_irq_cfg_writable(ARCH_IRQ_GERROR)) {
                m_arch_gerror_msi.data = value;
            }
            break;
        case SMMUV3_GERROR_IRQ_CFG2:
            if (arch_irq_cfg_writable(ARCH_IRQ_GERROR)) {
                m_arch_gerror_msi.attr = value & ARCH_MSI_CFG2_WRITABLE_MASK;
            }
            break;
        case SMMUV3_STRTAB_BASE_LO:
            write_u64_part(m_arch_strtab_base, false, value);
            break;
        case SMMUV3_STRTAB_BASE_HI:
            write_u64_part(m_arch_strtab_base, true, value);
            break;
        case SMMUV3_STRTAB_BASE_CFG:
            m_arch_strtab_cfg = value;
            break;
        case SMMUV3_CMDQ_BASE_LO:
            write_u64_part(m_cmdq.base, false, value);
            break;
        case SMMUV3_CMDQ_BASE_HI:
            write_u64_part(m_cmdq.base, true, value);
            break;
        case SMMUV3_CMDQ_PROD:
            m_cmdq.prod = value;
            process_cmdq();
            break;
        case SMMUV3_CMDQ_CONS:
            write_cmdq_cons(value);
            break;
        case SMMUV3_EVENTQ_BASE_LO:
            write_u64_part(m_eventq.base, false, value);
            break;
        case SMMUV3_EVENTQ_BASE_HI:
            write_u64_part(m_eventq.base, true, value);
            break;
        case SMMUV3_EVENTQ_PROD:
            m_eventq.prod = value & ARCH_QUEUE_INDEX_MASK;
            break;
        case SMMUV3_EVENTQ_CONS:
            write_arch_output_queue_cons(m_eventq, value);
            drain_stall_event_buffer();
            if (m_eventq.cons == m_eventq.prod) {
                clear_arch_irq_status(ARCH_IRQ_EVENTQ);
            }
            break;
        case SMMUV3_EVENTQ_IRQ_CFG0:
            if (arch_irq_cfg_writable(ARCH_IRQ_EVENTQ)) {
                write_msi_cfg0_part(m_arch_eventq_msi, false, value);
            }
            break;
        case SMMUV3_EVENTQ_IRQ_CFG0_HI:
            if (arch_irq_cfg_writable(ARCH_IRQ_EVENTQ)) {
                write_msi_cfg0_part(m_arch_eventq_msi, true, value);
            }
            break;
        case SMMUV3_EVENTQ_IRQ_CFG1:
            if (arch_irq_cfg_writable(ARCH_IRQ_EVENTQ)) {
                m_arch_eventq_msi.data = value;
            }
            break;
        case SMMUV3_EVENTQ_IRQ_CFG2:
            if (arch_irq_cfg_writable(ARCH_IRQ_EVENTQ)) {
                m_arch_eventq_msi.attr = value & ARCH_MSI_CFG2_WRITABLE_MASK;
            }
            break;
        case SMMUV3_PRIQ_BASE_LO:
            write_u64_part(m_priq.base, false, value);
            break;
        case SMMUV3_PRIQ_BASE_HI:
            write_u64_part(m_priq.base, true, value);
            break;
        case SMMUV3_PRIQ_PROD:
            m_priq.prod = value & ARCH_QUEUE_INDEX_MASK;
            break;
        case SMMUV3_PRIQ_CONS:
            write_arch_output_queue_cons(m_priq, value);
            if (m_priq.cons == m_priq.prod) {
                clear_arch_irq_status(ARCH_IRQ_PRIQ);
            }
            break;
        case SMMUV3_PRIQ_IRQ_CFG0:
            if (arch_irq_cfg_writable(ARCH_IRQ_PRIQ)) {
                write_msi_cfg0_part(m_arch_priq_msi, false, value);
            }
            break;
        case SMMUV3_PRIQ_IRQ_CFG0_HI:
            if (arch_irq_cfg_writable(ARCH_IRQ_PRIQ)) {
                write_msi_cfg0_part(m_arch_priq_msi, true, value);
            }
            break;
        case SMMUV3_PRIQ_IRQ_CFG1:
            if (arch_irq_cfg_writable(ARCH_IRQ_PRIQ)) {
                m_arch_priq_msi.data = value;
            }
            break;
        case SMMUV3_PRIQ_IRQ_CFG2:
            if (arch_irq_cfg_writable(ARCH_IRQ_PRIQ)) {
                m_arch_priq_msi.attr = value & ARCH_MSI_CFG2_WRITABLE_MASK;
            }
            break;
        case SMMUV3_GATOS_CTRL:
            m_arch_gatos_ctrl = value & ARCH_GATOS_CTRL_RUN;
            if ((m_arch_gatos_ctrl & ARCH_GATOS_CTRL_RUN) != 0) {
                run_arch_gatos_register_translate();
            }
            break;
        case SMMUV3_GATOS_SID_LO:
            write_u64_part(m_arch_gatos_sid, false, value);
            break;
        case SMMUV3_GATOS_SID_HI:
            write_u64_part(m_arch_gatos_sid, true, value);
            break;
        case SMMUV3_GATOS_ADDR_LO:
            write_u64_part(m_arch_gatos_addr, false, value);
            break;
        case SMMUV3_GATOS_ADDR_HI:
            write_u64_part(m_arch_gatos_addr, true, value);
            break;
        case SMMUV3_GATOS_PAR_LO:
        case SMMUV3_GATOS_PAR_HI:
            break;
        default:
            break;
        }
    }

    uint32_t read_reg(uint64_t addr) const
    {
        if (is_smmuv3_reg(addr)) {
            return read_smmuv3_reg(addr);
        }

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
        case REG_MAP_STREAM_ID:
            return selected_map_stream_id();
        case REG_MAP_STREAM_COUNT:
            return map_count(selected_map_stream_id());
        case REG_ARCH_STREAM_ID:
            return selected_arch_stream_id();
        case REG_ARCH_ATS_DETAIL:
            return arch_ats_detail();
        case REG_ARCH_PRI_DETAIL:
            return arch_pri_detail();
        case REG_ARCH_FAULT_DETAIL:
            return m_arch_last_fault_detail;
        case REG_ARCH_STALL_STATUS:
            return arch_stall_status();
        case REG_ARCH_STALL_MERGE_STATUS:
            return arch_stall_merge_status();
        case REG_ARCH_IPA_LO:
            return static_cast<uint32_t>(m_arch_last_ipa);
        case REG_ARCH_IPA_HI:
            return static_cast<uint32_t>(m_arch_last_ipa >> 32);
        case REG_ARCH_CMD_STATUS:
            return arch_cmd_status();
        case REG_ARCH_CMD_DETAIL:
            return arch_cmd_detail();
        case REG_ARCH_SSID:
            return (m_arch_selected_ssid & ARCH_CMDQ_SSID_MASK) |
                   (m_arch_selected_ssid_valid ? (1u << 31) : 0u);
        case REG_ARCH_CD_DETAIL:
            return arch_cd_detail();
        case REG_ARCH_ENDPOINT_REPLAY_STATUS:
            return arch_endpoint_replay_status();
        case REG_ARCH_ENDPOINT_REPLAY_CTRL:
            return arch_endpoint_replay_ctrl();
        case REG_ARCH_ENDPOINT_BLOCK_STATUS:
            return arch_endpoint_block_status();
        case REG_ARCH_EARLY_RETRY_STATUS:
            return arch_early_retry_status();
        case REG_ARCH_MPAM_STATUS:
            return arch_mpam_status();
        case REG_ARCH_MPAM_DETAIL:
            return m_arch_last_mpam_partid;
        case REG_ARCH_SECURITY_STATUS:
            return arch_security_status();
        case REG_ARCH_PAR_LO:
            return static_cast<uint32_t>(m_arch_last_par);
        case REG_ARCH_PAR_HI:
            return static_cast<uint32_t>(m_arch_last_par >> 32);
        default:
            return 0;
        }
    }

    void write_reg(uint64_t addr, uint32_t value)
    {
        if (is_smmuv3_reg(addr)) {
            write_smmuv3_reg(addr, value);
            return;
        }

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
        case REG_MAP_STREAM_ID:
            m_map_stream_id = value;
            m_map_stream_id_valid = true;
            break;
        case REG_ARCH_STREAM_ID:
            m_arch_stream_id = value;
            m_arch_stream_id_valid = true;
            break;
        case REG_ARCH_SSID:
            m_arch_selected_ssid = value & ARCH_CMDQ_SSID_MASK;
            m_arch_selected_ssid_valid = (value & (1u << 31)) != 0;
            break;
        case REG_ARCH_ENDPOINT_REPLAY_CTRL:
            m_arch_endpoint_blocking_enabled =
                (value & ARCH_ENDPOINT_REPLAY_BLOCKING_ENABLE) != 0;
            if ((value & ARCH_ENDPOINT_REPLAY_EARLY_RETRY) != 0) {
                early_retry_endpoint_replays();
            }
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
                log_map_error("ctrl", selected_map_stream_id(), m_map_iova, m_map_pa, m_map_size);
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
            } else if (value == ARCH_CTRL_PROBE_WRITE) {
                run_arch_probe(true);
            } else if (value == ARCH_CTRL_ATS_TRANSLATION_REQUEST) {
                run_arch_ats_translation_request();
            } else if (value == ARCH_CTRL_ATS_TRANSLATION_REQUEST_WRITE) {
                run_arch_ats_translation_request(true);
            } else if (value == ARCH_CTRL_NEGATIVE_REPLAY) {
                run_arch_negative_replay();
            } else if (value == ARCH_CTRL_NEGATIVE_REPLAY_WRITE) {
                run_arch_negative_replay(true);
            } else if (value == ARCH_CTRL_RECORD_F_UUT) {
                run_arch_f_uut_event();
            } else if (value == ARCH_CTRL_TLB_CONFLICT) {
                run_arch_tlb_conflict_probe();
            } else if (value == ARCH_CTRL_CFG_CONFLICT) {
                run_arch_cfg_conflict_probe();
            } else if (value == ARCH_CTRL_GATOS_TRANSLATE) {
                run_arch_gatos_translate();
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

    void log_translate(uint32_t stream_id, const char* op, uint64_t iova, uint64_t pa, uint64_t len)
    {
        SCP_INFO(()) << "APOLLO_SMMU_TBU: stream-id=0x" << std::hex << stream_id << " translate " << op
                     << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: stream-id=0x" << std::hex << stream_id << " translate " << op
                  << " iova=0x" << iova << " pa=0x" << pa << " len=0x" << len << std::dec << std::endl;
    }

    uint16_t log_fault(uint32_t stream_id, const char* op, uint64_t iova, uint64_t len,
                       bool ssid_valid = false, uint32_t ssid = 0,
                       const uint8_t* payload = nullptr, size_t payload_len = 0,
                       bool privileged = false, bool instruction = false)
    {
        const bool write = std::strcmp(op, "write") == 0;

        if (m_arch_fault_reason == ARCH_FAULT_NONE) {
            m_arch_fault_reason = ARCH_FAULT_PAGE_INVALID;
        }
        if (m_arch_fault_stage == ARCH_FAULT_STAGE_NONE) {
            m_arch_fault_stage = ARCH_FAULT_STAGE_S1;
        }
        const uint16_t stag = record_fault(stream_id, op, iova, len, true,
                                           ssid_valid, ssid, privileged, instruction);
        allocate_endpoint_replay_record(stream_id, stag, iova, len, write,
                                        ssid_valid, ssid, payload, payload_len);
        SCP_WARN(()) << "APOLLO_SMMU_TBU: translation fault stream-id=0x" << std::hex << stream_id
                     << " " << op << " iova=0x" << iova << " len=0x" << len;
        if (ssid_valid) {
            SCP_WARN(()) << " endpoint-ssid=0x" << std::hex << ssid;
        }
        SCP_WARN(()) << " window=[0x" << std::hex << p_iova_base.get_value() << "..0x"
                     << (p_iova_base.get_value() + p_window_size.get_value()) << ")" << std::dec;
        std::cerr << "APOLLO_SMMU_TBU: translation fault stream-id=0x" << std::hex << stream_id
                  << " " << op << " iova=0x" << iova << " len=0x" << len;
        if (ssid_valid) {
            std::cerr << " endpoint-ssid=0x" << ssid;
        }
        std::cerr << " window=[0x" << p_iova_base.get_value() << "..0x"
                  << (p_iova_base.get_value() + p_window_size.get_value()) << ")"
                  << std::dec << std::endl;
        return stag;
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        const uint64_t iova = trans.get_address();
        const uint64_t len = trans.get_data_length();
        const char* op = trans.is_read() ? "read" : (trans.is_write() ? "write" : "op");
        auto* const data = trans.get_data_ptr();
        const unsigned int streaming_width = trans.get_streaming_width();
        uint64_t offset = 0;

        const uint32_t stream_id = transaction_stream_id(trans);
        uint32_t ssid = 0;
        const bool ssid_valid = transaction_substream_id(trans, ssid);
        const bool privileged = transaction_privileged(trans);
        const bool instruction = transaction_instruction(trans);
        const bool translated = transaction_translated(trans);
        const uint8_t security_state = transaction_security_state(trans);

        m_arch_translated_split_stage2_request_active = false;
        m_arch_translated_effective_access_valid = false;
        m_arch_translated_effective_privileged = privileged;
        m_arch_translated_effective_instruction = instruction;
        m_arch_current_access_privileged = privileged;
        m_arch_current_access_instruction = instruction;
        m_arch_fault_record_suppressed = false;
        m_arch_last_security_state = security_state;
        m_arch_last_security_supported = arch_security_state_supported(security_state);
        if (!m_arch_last_security_supported) {
            m_arch_status = ARCH_STATUS_ERROR;
            m_arch_fault_reason = ARCH_FAULT_UNSUPPORTED_UPSTREAM;
            m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
            m_arch_fault_event_class = ARCH_EVENT_CLASS_IN;
            m_arch_fault_nsipa = false;
            m_arch_fault_gpcf = false;
            m_arch_fault_record_suppressed = false;
            record_fault(stream_id, "unsupported-security-state", iova, len, false,
                         ssid_valid, ssid, privileged, instruction);
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        if (translated &&
            !allow_arch_translated_transaction(stream_id, op, iova, len, ssid_valid,
                                               ssid, privileged, instruction)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        while (offset < len) {
            uint64_t pa = 0;
            uint64_t segment_len = 0;

            if (!translate_segment(stream_id, iova + offset, len - offset, pa, segment_len,
                                   ssid_valid, ssid, trans.is_write(), translated,
                                   translated)) {
                if (m_arch_fault_record_suppressed) {
                    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                    break;
                }
                const bool fault_privileged =
                    m_arch_translated_effective_access_valid ?
                        m_arch_translated_effective_privileged :
                        privileged;
                const bool fault_instruction =
                    m_arch_translated_effective_access_valid ?
                        m_arch_translated_effective_instruction :
                        instruction;
                const uint16_t stag =
                    log_fault(stream_id, op, iova + offset, len - offset, ssid_valid, ssid,
                              trans.is_write() ? data + offset : nullptr,
                              trans.is_write() ? static_cast<size_t>(len - offset) : 0,
                              fault_privileged, fault_instruction);
                if (m_arch_endpoint_blocking_enabled && stag != 0) {
                    trans.set_response_status(
                        wait_endpoint_replay_resume(stream_id, stag, trans.is_read(),
                                                    data + offset,
                                                    static_cast<size_t>(len - offset)));
                } else {
                    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                }
                break;
            }

            log_translate(stream_id, op, iova + offset, pa, segment_len);
            auto* ext = trans.get_extension<gs::ApolloSmmuStreamIdExtension>();
            const bool restore_mpam = ext != nullptr && m_arch_last_mpam_valid;
            const bool restore_output_attrs =
                ext != nullptr && m_arch_last_output_attrs_valid;
            bool old_mpam_valid = false;
            bool old_mpam_remapped = false;
            bool old_mpam_unknown = false;
            uint8_t old_mpam_partid_space = ARCH_MPAM_SPACE_NONSECURE;
            uint16_t old_mpam_partid = 0;
            uint8_t old_mpam_pmg = 0;
            bool old_output_attrs_valid = false;
            bool old_output_mtcfg = false;
            uint8_t old_output_mem_type = 0;
            uint8_t old_output_shareability = 0;
            uint8_t old_output_alloc_hint = 0;
            uint8_t old_output_inst_cfg = 0;
            uint8_t old_output_priv_cfg = 0;
            uint8_t old_output_ns_cfg = 0;
            bool old_privileged = false;
            bool old_instruction = false;
            if (restore_mpam) {
                old_mpam_valid = ext->mpam_valid;
                old_mpam_remapped = ext->mpam_remapped;
                old_mpam_unknown = ext->mpam_unknown;
                old_mpam_partid_space = ext->mpam_partid_space;
                old_mpam_partid = ext->mpam_partid;
                old_mpam_pmg = ext->mpam_pmg;
                ext->mpam_valid = true;
                ext->mpam_remapped = m_arch_last_mpam_remapped;
                ext->mpam_unknown = m_arch_last_mpam_unknown;
                ext->mpam_partid_space = m_arch_last_mpam_partid_space;
                ext->mpam_partid = m_arch_last_mpam_partid;
                ext->mpam_pmg = m_arch_last_mpam_pmg;
            }
            if (restore_output_attrs) {
                old_output_attrs_valid = ext->output_attrs_valid;
                old_output_mtcfg = ext->output_mtcfg;
                old_output_mem_type = ext->output_mem_type;
                old_output_shareability = ext->output_shareability;
                old_output_alloc_hint = ext->output_alloc_hint;
                old_output_inst_cfg = ext->output_inst_cfg;
                old_output_priv_cfg = ext->output_priv_cfg;
                old_output_ns_cfg = ext->output_ns_cfg;
                populate_arch_output_attrs_extension(*ext);
            }
            if (ext != nullptr && m_arch_translated_effective_access_valid) {
                old_privileged = ext->privileged;
                old_instruction = ext->instruction;
                ext->privileged = m_arch_translated_effective_privileged;
                ext->instruction = m_arch_translated_effective_instruction;
            }
            trans.set_address(pa);
            trans.set_data_ptr(data + offset);
            trans.set_data_length(segment_len);
            trans.set_streaming_width(segment_len);
            downstream->b_transport(trans, delay);
            if (ext != nullptr && m_arch_translated_effective_access_valid) {
                ext->privileged = old_privileged;
                ext->instruction = old_instruction;
            }
            if (restore_output_attrs) {
                ext->output_attrs_valid = old_output_attrs_valid;
                ext->output_mtcfg = old_output_mtcfg;
                ext->output_mem_type = old_output_mem_type;
                ext->output_shareability = old_output_shareability;
                ext->output_alloc_hint = old_output_alloc_hint;
                ext->output_inst_cfg = old_output_inst_cfg;
                ext->output_priv_cfg = old_output_priv_cfg;
                ext->output_ns_cfg = old_output_ns_cfg;
            }
            if (restore_mpam) {
                ext->mpam_valid = old_mpam_valid;
                ext->mpam_remapped = old_mpam_remapped;
                ext->mpam_unknown = old_mpam_unknown;
                ext->mpam_partid_space = old_mpam_partid_space;
                ext->mpam_partid = old_mpam_partid;
                ext->mpam_pmg = old_mpam_pmg;
            }
            if (!trans.is_response_ok()) {
                break;
            }
            offset += segment_len;
        }

        trans.set_address(iova);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(streaming_width);
        m_arch_translated_split_stage2_request_active = false;
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        const uint64_t iova = trans.get_address();
        const uint64_t len = trans.get_data_length();
        auto* const data = trans.get_data_ptr();
        const unsigned int streaming_width = trans.get_streaming_width();
        uint64_t offset = 0;
        unsigned int total = 0;

        const uint32_t stream_id = transaction_stream_id(trans);
        uint32_t ssid = 0;
        const bool ssid_valid = transaction_substream_id(trans, ssid);
        const uint8_t security_state = transaction_security_state(trans);

        m_arch_last_security_state = security_state;
        m_arch_last_security_supported = arch_security_state_supported(security_state);
        if (!m_arch_last_security_supported) {
            m_arch_status = ARCH_STATUS_ERROR;
            m_arch_fault_reason = ARCH_FAULT_UNSUPPORTED_UPSTREAM;
            m_arch_fault_stage = ARCH_FAULT_STAGE_NONE;
            return 0;
        }

        while (offset < len) {
            uint64_t pa = 0;
            uint64_t segment_len = 0;
            unsigned int ret = 0;

            if (!translate_segment(stream_id, iova + offset, len - offset, pa, segment_len,
                                   ssid_valid, ssid)) {
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

inline bool apollo_smmu_tbu::default_arch_io_executor::read_descriptor(
    apollo_smmu_tbu& tbu,
    const arch_descriptor_memory_read& read,
    uint64_t& desc)
{
    return tbu.read_downstream_u64(read.transaction_pa, desc, false, true);
}

inline tlm::tlm_response_status
apollo_smmu_tbu::default_arch_io_executor::replay_transaction(
    apollo_smmu_tbu& tbu,
    const arch_endpoint_replay_transaction& transaction,
    sc_core::sc_time& delay)
{
    return tbu.execute_endpoint_replay_transaction_tlm(transaction, delay);
}

extern "C" void module_register();
