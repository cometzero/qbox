/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include <mmu720ae_regs.h>

namespace qbox {
namespace mmu720ae {

enum class access_status {
    ok,
    address_error,
    command_error,
};

struct event_record {
    std::array<uint64_t, EVTQ_ENT_DWORDS> dwords {};
};

class core
{
public:
    static constexpr uint32_t REG_BYTES = ARM_SMMU_PAGE1_OFFSET + ARM_SMMU_REG_SZ;

    core();

    void reset();
    access_status read(uint64_t offset, uint8_t* data, uint32_t len,
                       bool debug = false);
    access_status write(uint64_t offset, const uint8_t* data, uint32_t len,
                        bool debug = false);

    uint32_t load32_for_test(uint32_t offset) const;
    uint64_t load64_for_test(uint32_t offset) const;

    bool combined_irq_level() const { return m_combined_irq; }
    uint64_t cmdq_sync_count() const { return m_cmdq_sync_count; }
    uint64_t dmi_generation() const { return m_dmi_generation; }
    uint64_t tbu_request_count() const { return m_tbu_request_count; }
    uint64_t tbu_fallback_sid_count() const { return m_tbu_fallback_sid_count; }
    uint64_t event_record_count() const { return m_event_record_count; }
    uint64_t event_queue_abort_count() const { return m_event_queue_abort_count; }
    uint32_t last_tbu_sid() const { return m_last_tbu_sid; }
    bool smmu_enabled() const;
    bool build_translation_fault_event(uint64_t iova, bool read, uint32_t sid,
                                       event_record& record,
                                       uint64_t& write_address) const;
    void record_tbu_request_sid(uint32_t sid, bool fallback);
    void complete_event_queue_write(bool ok);
    void record_event_queue_abort();

private:
    std::array<uint8_t, REG_BYTES> m_regs {};
    bool m_combined_irq = false;
    uint64_t m_cmdq_sync_count = 0;
    uint64_t m_dmi_generation = 0;
    uint64_t m_tbu_request_count = 0;
    uint64_t m_tbu_fallback_sid_count = 0;
    uint64_t m_event_record_count = 0;
    uint64_t m_event_queue_abort_count = 0;
    uint32_t m_last_tbu_sid = 0;

    static bool supported_length(uint32_t len);
    static bool normalize_offset(uint64_t offset, uint32_t len,
                                 uint32_t& normalized);

    uint32_t load32(uint32_t offset) const;
    uint64_t load64(uint32_t offset) const;
    void store32(uint32_t offset, uint32_t value);
    void store64(uint32_t offset, uint64_t value);
    void write32(uint32_t offset, uint32_t value);
    void write64(uint32_t offset, uint64_t value);
    bool event_queue_enabled() const;
    bool event_queue_configured() const;
    bool event_queue_full() const;
    bool event_queue_pending() const;
    uint32_t event_queue_log2_entries() const;
    uint32_t event_queue_index(uint32_t pointer) const;
    uint32_t event_queue_wrap(uint32_t pointer) const;
    uint32_t event_queue_next_prod() const;
    uint64_t event_queue_base() const;
    uint64_t event_queue_prod_entry_address() const;
    void update_irq();
    void invalidate_cached_translations();

    static constexpr uint32_t idr0_value()
    {
        return IDR0_STALL_MODEL_NONE | IDR0_TTENDIAN_LE | IDR0_COHACC |
               IDR0_TTF_AARCH64 | IDR0_S1P;
    }

    static constexpr uint32_t idr1_value()
    {
        return (8u << IDR1_CMDQS_SHIFT) | (8u << IDR1_EVTQS_SHIFT) |
               (0u << IDR1_PRIQS_SHIFT) | (0u << IDR1_SSIDSIZE_SHIFT) |
               8u;
    }

    static constexpr uint32_t idr5_value()
    {
        return IDR5_GRAN4K | IDR5_OAS_48_BIT;
    }
};

} // namespace mmu720ae
} // namespace qbox
