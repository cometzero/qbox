/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_CPU_PC_ENTRY_OBSERVER_H
#define _LIBQBOX_COMPONENTS_CPU_PC_ENTRY_OBSERVER_H

#include <cstdint>
#include <iosfwd>

namespace qemu {
class Cpu;
}

class QemuCpuPcEntryObserver
{
public:
    virtual ~QemuCpuPcEntryObserver() = default;

    virtual bool enabled() const = 0;
    virtual bool needs_pc_entry_callback() const { return enabled(); }
    virtual void configure_pc_watches(qemu::Cpu& cpu) = 0;
    virtual bool on_pc_entry(uint64_t pc) = 0;
    virtual void on_cpu_sync() {}
    virtual void end_of_simulation() {}
    virtual void write_profile_json(std::ostream&) const {}
};

#endif
