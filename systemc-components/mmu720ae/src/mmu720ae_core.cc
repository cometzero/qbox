/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <mmu720ae_core.h>

namespace qbox {
namespace mmu720ae {

core::core()
{
    reset();
}

void core::reset()
{
    m_regs.fill(0);
    m_combined_irq = false;
    m_cmdq_sync_count = 0;
    m_tbu_request_count = 0;
    m_tbu_fallback_sid_count = 0;
    m_event_record_count = 0;
    m_event_queue_abort_count = 0;
    m_last_tbu_sid = 0;
    ++m_dmi_generation;

    store32(ARM_SMMU_IDR0, idr0_value());
    store32(ARM_SMMU_IDR1, idr1_value());
    store32(ARM_SMMU_IDR3, 0);
    store32(ARM_SMMU_IDR5, idr5_value());
    store32(ARM_SMMU_IIDR, 0x720ae000);
    store32(ARM_SMMU_AIDR, 0x00000003);
    store32(ARM_SMMU_GERRORN, GERROR_ERR_MASK);
}

bool core::supported_length(uint32_t len)
{
    return len == 1 || len == 2 || len == 4 || len == 8;
}

bool core::normalize_offset(uint64_t offset, uint32_t len,
                            uint32_t& normalized)
{
    if (!supported_length(len)) {
        return false;
    }

    if (offset >= ARM_SMMU_PAGE1_OFFSET &&
        offset < ARM_SMMU_PAGE1_OFFSET + ARM_SMMU_REG_SZ) {
        offset -= ARM_SMMU_PAGE1_OFFSET;
    }

    if (offset + len > ARM_SMMU_REG_SZ) {
        return false;
    }

    normalized = static_cast<uint32_t>(offset);
    return true;
}

uint32_t core::load32(uint32_t offset) const
{
    uint32_t value = 0;
    std::memcpy(&value, &m_regs[offset], sizeof(value));
    return value;
}

uint64_t core::load64(uint32_t offset) const
{
    uint64_t value = 0;
    std::memcpy(&value, &m_regs[offset], sizeof(value));
    return value;
}

void core::store32(uint32_t offset, uint32_t value)
{
    std::memcpy(&m_regs[offset], &value, sizeof(value));

    if (offset < ARM_SMMU_REG_SZ &&
        offset + sizeof(value) <= ARM_SMMU_REG_SZ) {
        std::memcpy(&m_regs[ARM_SMMU_PAGE1_OFFSET + offset],
                    &value,
                    sizeof(value));
    }
}

void core::store64(uint32_t offset, uint64_t value)
{
    std::memcpy(&m_regs[offset], &value, sizeof(value));

    if (offset < ARM_SMMU_REG_SZ &&
        offset + sizeof(value) <= ARM_SMMU_REG_SZ) {
        std::memcpy(&m_regs[ARM_SMMU_PAGE1_OFFSET + offset],
                    &value,
                    sizeof(value));
    }
}

uint32_t core::load32_for_test(uint32_t offset) const
{
    return load32(offset);
}

uint64_t core::load64_for_test(uint32_t offset) const
{
    return load64(offset);
}

bool core::smmu_enabled() const
{
    return (load32(ARM_SMMU_CR0ACK) & CR0_SMMUEN) != 0;
}

bool core::event_queue_enabled() const
{
    return (load32(ARM_SMMU_CR0ACK) & CR0_EVTQEN) != 0;
}

uint32_t core::event_queue_log2_entries() const
{
    return static_cast<uint32_t>(load64(ARM_SMMU_EVTQ_BASE) &
                                 Q_BASE_LOG2SIZE_MASK);
}

bool core::event_queue_configured() const
{
    return event_queue_enabled() && event_queue_log2_entries() > 0 &&
           event_queue_log2_entries() < 31 && event_queue_base() != 0;
}

uint64_t core::event_queue_base() const
{
    return load64(ARM_SMMU_EVTQ_BASE) & Q_BASE_ADDR_MASK;
}

uint32_t core::event_queue_index(uint32_t pointer) const
{
    const uint32_t mask = (1u << event_queue_log2_entries()) - 1u;
    return pointer & mask;
}

uint32_t core::event_queue_wrap(uint32_t pointer) const
{
    return pointer & (1u << event_queue_log2_entries());
}

bool core::event_queue_pending() const
{
    if (!event_queue_configured()) {
        return false;
    }

    return load32(ARM_SMMU_EVTQ_PROD) != load32(ARM_SMMU_EVTQ_CONS);
}

bool core::event_queue_full() const
{
    if (!event_queue_configured()) {
        return true;
    }

    const uint32_t prod = load32(ARM_SMMU_EVTQ_PROD);
    const uint32_t cons = load32(ARM_SMMU_EVTQ_CONS);
    return event_queue_index(prod) == event_queue_index(cons) &&
           event_queue_wrap(prod) != event_queue_wrap(cons);
}

uint32_t core::event_queue_next_prod() const
{
    const uint32_t prod = load32(ARM_SMMU_EVTQ_PROD);
    const uint32_t log2_entries = event_queue_log2_entries();
    const uint32_t mask = (1u << log2_entries) - 1u;
    const uint32_t wrap_bit = 1u << log2_entries;
    uint32_t index = prod & mask;
    uint32_t wrap = prod & wrap_bit;

    ++index;
    if (index == (1u << log2_entries)) {
        index = 0;
        wrap ^= wrap_bit;
    }

    return wrap | index;
}

uint64_t core::event_queue_prod_entry_address() const
{
    return event_queue_base() +
           static_cast<uint64_t>(event_queue_index(load32(ARM_SMMU_EVTQ_PROD))) *
               EVTQ_ENT_BYTES;
}

void core::invalidate_cached_translations()
{
    ++m_dmi_generation;
}

bool core::build_translation_fault_event(uint64_t iova, bool read, uint32_t sid,
                                         event_record& record,
                                         uint64_t& write_address) const
{
    if (!event_queue_configured() || event_queue_full()) {
        return false;
    }

    record = {};
    record.dwords[0] = EVT_ID_TRANSLATION_FAULT & EVTQ_0_ID_MASK;
    record.dwords[0] |= static_cast<uint64_t>(sid) << EVTQ_0_SID_SHIFT;
    record.dwords[2] = iova;
    if (read) {
        record.dwords[1] |= 1ull << EVTQ_1_RNW_SHIFT;
    }
    write_address = event_queue_prod_entry_address();
    return true;
}

void core::record_tbu_request_sid(uint32_t sid, bool fallback)
{
    ++m_tbu_request_count;
    if (fallback) {
        ++m_tbu_fallback_sid_count;
    }
    m_last_tbu_sid = sid;
}

void core::complete_event_queue_write(bool ok)
{
    if (!ok) {
        record_event_queue_abort();
        return;
    }

    ++m_event_record_count;
    store32(ARM_SMMU_EVTQ_PROD, event_queue_next_prod());
    update_irq();
}

void core::record_event_queue_abort()
{
    ++m_event_queue_abort_count;
    store32(ARM_SMMU_GERROR, load32(ARM_SMMU_GERROR) | GERROR_EVTQ_ABT_ERR);
    store32(ARM_SMMU_GERRORN, GERROR_ERR_MASK & ~load32(ARM_SMMU_GERROR));
    update_irq();
}

void core::update_irq()
{
    const uint32_t irq_ctrl = load32(ARM_SMMU_IRQ_CTRL);
    const uint32_t gerror = load32(ARM_SMMU_GERROR);

    m_combined_irq =
        ((irq_ctrl & IRQ_CTRL_GERROR_IRQEN) != 0 &&
         (gerror & GERROR_ERR_MASK) != 0) ||
        ((irq_ctrl & IRQ_CTRL_EVTQ_IRQEN) != 0 && event_queue_pending());
}

void core::write32(uint32_t offset, uint32_t value)
{
    switch (offset) {
    case ARM_SMMU_IDR0:
    case ARM_SMMU_IDR1:
    case ARM_SMMU_IDR3:
    case ARM_SMMU_IDR5:
    case ARM_SMMU_IIDR:
    case ARM_SMMU_AIDR:
    case ARM_SMMU_CR0ACK:
    case ARM_SMMU_IRQ_CTRLACK:
        break;
    case ARM_SMMU_CR0:
    {
        const uint32_t implemented = value & CR0_IMPLEMENTED_MASK;
        store32(ARM_SMMU_CR0, implemented);
        store32(ARM_SMMU_CR0ACK, implemented);
        if ((implemented & CR0_SMMUEN) == 0) {
            invalidate_cached_translations();
        }
        break;
    }
    case ARM_SMMU_IRQ_CTRL:
        store32(ARM_SMMU_IRQ_CTRL, value & (IRQ_CTRL_EVTQ_IRQEN |
                                            IRQ_CTRL_GERROR_IRQEN));
        store32(ARM_SMMU_IRQ_CTRLACK, load32(ARM_SMMU_IRQ_CTRL));
        update_irq();
        break;
    case ARM_SMMU_GBPA:
        store32(ARM_SMMU_GBPA, value & ~GBPA_UPDATE);
        break;
    case ARM_SMMU_GERRORN:
        store32(ARM_SMMU_GERROR, load32(ARM_SMMU_GERROR) & ~value);
        store32(ARM_SMMU_GERRORN, GERROR_ERR_MASK & ~load32(ARM_SMMU_GERROR));
        update_irq();
        break;
    case ARM_SMMU_CMDQ_PROD:
        store32(ARM_SMMU_CMDQ_PROD, value);
        store32(ARM_SMMU_CMDQ_CONS, value);
        ++m_cmdq_sync_count;
        invalidate_cached_translations();
        break;
    case ARM_SMMU_EVTQ_CONS:
        store32(ARM_SMMU_EVTQ_CONS, value);
        update_irq();
        break;
    case ARM_SMMU_PRIQ_CONS:
        store32(ARM_SMMU_PRIQ_CONS, value);
        update_irq();
        break;
    default:
        store32(offset, value);
        break;
    }
}

void core::write64(uint32_t offset, uint64_t value)
{
    switch (offset) {
    case ARM_SMMU_STRTAB_BASE:
    case ARM_SMMU_CMDQ_BASE:
    case ARM_SMMU_EVTQ_BASE:
    case ARM_SMMU_EVTQ_IRQ_CFG0:
    case ARM_SMMU_PRIQ_BASE:
    case ARM_SMMU_PRIQ_IRQ_CFG0:
    case ARM_SMMU_GERROR_IRQ_CFG0:
        store64(offset, value);
        break;
    default:
        store64(offset, value);
        break;
    }
}

access_status core::read(uint64_t offset, uint8_t* data, uint32_t len,
                         bool debug)
{
    (void)debug;

    uint32_t normalized = 0;
    if (data == nullptr || !normalize_offset(offset, len, normalized)) {
        return access_status::address_error;
    }

    std::memcpy(data, &m_regs[normalized], len);
    return access_status::ok;
}

access_status core::write(uint64_t offset, const uint8_t* data, uint32_t len,
                          bool debug)
{
    (void)debug;

    uint32_t normalized = 0;
    if (data == nullptr || !normalize_offset(offset, len, normalized)) {
        return access_status::address_error;
    }

    if (len == sizeof(uint32_t) && (normalized % sizeof(uint32_t)) == 0) {
        uint32_t value = 0;
        std::memcpy(&value, data, sizeof(value));
        write32(normalized, value);
    } else if (len == sizeof(uint64_t) &&
               (normalized % sizeof(uint64_t)) == 0) {
        uint64_t value = 0;
        std::memcpy(&value, data, sizeof(value));
        write64(normalized, value);
    } else {
        std::memcpy(&m_regs[normalized], data, len);
    }

    return access_status::ok;
}

} // namespace mmu720ae
} // namespace qbox
