/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_CPU_SEMANTIC_CONTEXT_H
#define _LIBQBOX_COMPONENTS_CPU_SEMANTIC_CONTEXT_H

#include <cstdint>
#include <vector>

#include "libqemu-cxx/target/aarch64.h"

class QemuCpuSemanticContext
{
public:
    virtual ~QemuCpuSemanticContext() = default;

    virtual uint64_t get_pc() const = 0;
    virtual uint64_t get_v7m_state(qemu::CpuArm::V7MStateField field) const = 0;
    virtual bool set_v7m_state(qemu::CpuArm::V7MStateField field, uint64_t value) = 0;
    virtual bool guest_dmi_ptr(uint64_t address, uint64_t size, bool need_read,
                               bool need_write, uint8_t*& ptr) = 0;
    virtual bool guest_read_bytes(uint64_t address, uint64_t size,
                                  std::vector<uint8_t>& out) = 0;
    virtual bool guest_write_bytes(uint64_t address, const uint8_t* data,
                                   uint64_t size) = 0;
    virtual void invalidate_guest_range(uint64_t start, uint64_t end) = 0;
    virtual void set_vcpu_dirty(bool dirty) = 0;
    virtual void kick_cpu() = 0;
};

#endif
