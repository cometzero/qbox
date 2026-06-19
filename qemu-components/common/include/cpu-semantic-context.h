/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_CPU_SEMANTIC_CONTEXT_H
#define _LIBQBOX_COMPONENTS_CPU_SEMANTIC_CONTEXT_H

#include <cstdint>
#include <string>
#include <vector>

#include "libqemu-cxx/target/aarch64.h"

class QemuCpuSemanticContext
{
public:
    virtual ~QemuCpuSemanticContext() = default;

    virtual const char* semantic_cpu_name() const = 0;
    virtual uint64_t get_pc() const = 0;
    virtual uint64_t get_v7m_state(qemu::CpuArm::V7MStateField field) const = 0;
    virtual uint64_t get_aarch64_state(qemu::CpuArm::Aarch64StateField field) const = 0;
    virtual bool set_v7m_state(qemu::CpuArm::V7MStateField field, uint64_t value) = 0;
    virtual bool guest_dmi_ptr(uint64_t address, uint64_t size, bool need_read,
                               bool need_write, uint8_t*& ptr) = 0;
    virtual bool guest_read_u32(uint64_t address, uint32_t& value,
                                bool allow_tlm_fallback) = 0;
    virtual bool guest_read_u8(uint64_t address, uint8_t& value,
                               bool allow_tlm_fallback) = 0;
    virtual bool guest_read_bytes(uint64_t address, uint64_t size,
                                  std::vector<uint8_t>& out,
                                  bool allow_tlm_fallback) = 0;
    virtual bool guest_read_bytes_or_alias(uint64_t address, uint64_t size,
                                           std::vector<uint8_t>& out,
                                           bool& direct_file_alias,
                                           bool allow_tlm_fallback) = 0;
    virtual bool guest_write_bytes(uint64_t address, const uint8_t* data,
                                   uint64_t size, bool allow_tlm_fallback) = 0;
    virtual bool guest_write_bytes_or_alias(uint64_t address, const uint8_t* data,
                                            uint64_t size, bool& direct_file_alias,
                                            bool allow_tlm_fallback) = 0;
    virtual bool guest_write_u32(uint64_t address, uint32_t value,
                                 bool allow_tlm_fallback) = 0;
    virtual bool direct_file_alias_ptr(uint64_t address, uint64_t size,
                                       bool need_write, uint8_t*& ptr) = 0;
    virtual void install_direct_file_aliases(const std::string& aliases) = 0;
    virtual void install_mmio_read_fastpath(const std::string& spec) = 0;
    virtual void install_mmio_direct_fastpath_ranges(
        const std::string& spec) = 0;
    virtual void invalidate_guest_range(uint64_t start, uint64_t end) = 0;
    virtual void set_vcpu_dirty(bool dirty) = 0;
    virtual void kick_cpu() = 0;
    virtual uint64_t pc_entry_watch_count() const = 0;
    virtual uint64_t pc_entry_watch_add_calls() const = 0;
    virtual uint64_t pc_entry_watch_clear_calls() const = 0;
    virtual uint64_t pc_entry_watch_match_queries() const = 0;
    virtual uint64_t pc_entry_watch_match_hits() const = 0;
    virtual uintptr_t pc_entry_watch_last_pc() const = 0;
    virtual uintptr_t pc_entry_watch_last_watch_pc() const = 0;
};

#endif
