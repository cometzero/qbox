/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <libqemu-cxx/libqemu-cxx.h>
#include <internals.h>

namespace qemu {

static void generic_cpu_end_of_loop_cb(QemuObject* cpu, void* opaque)
{
    LibQemuInternals* internals = reinterpret_cast<LibQemuInternals*>(opaque);

    internals->get_cpu_end_of_loop_cb().call(cpu);
}

static void generic_cpu_kick_cb(QemuObject* cpu, void* opaque)
{
    LibQemuInternals* internals = reinterpret_cast<LibQemuInternals*>(opaque);

    internals->get_cpu_kick_cb().call(cpu);
}

static void generic_vm_state_cb(bool running, void* opaque)
{
    LibQemuInternals* internals = reinterpret_cast<LibQemuInternals*>(opaque);

    internals->call_vm_state_callback(running);
}

static bool generic_cpu_pc_entry_cb(QemuObject* cpu, uint64_t pc, void* opaque)
{
    LibQemuInternals* internals = reinterpret_cast<LibQemuInternals*>(opaque);

    return internals->get_cpu_pc_entry_cb().call_bool(cpu, static_cast<uintptr_t>(pc));
}

static IOMMUMemoryRegion::IOMMUTLBEntry generic_iommu_translate_cb(QemuObject* mr, void* opaque, uint64_t addr,
                                                                   IOMMUMemoryRegion::IOMMUAccessFlags flag, int idx)
{
    LibQemuInternals* internals = reinterpret_cast<LibQemuInternals*>(opaque);
    IOMMUMemoryRegion::IOMMUTLBEntry entry;
    internals->get_iommu_translate_cb().call(mr, &entry, addr, flag, idx);
    return entry;
}
static void generic_cpu_riscv_mip_update_cb(QemuObject* cpu, uint32_t value, void* opaque)
{
    LibQemuInternals* internals = reinterpret_cast<LibQemuInternals*>(opaque);

    internals->get_cpu_riscv_mip_update_cb().call(cpu, value);
}

void LibQemu::init_callbacks()
{
    m_int->exports().set_cpu_end_of_loop_cb(generic_cpu_end_of_loop_cb, m_int.get());

    m_int->exports().set_cpu_kick_cb(generic_cpu_kick_cb, m_int.get());

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    m_int->exports().set_iommu_translate_cb(reinterpret_cast<LibQemuIOMMUTranslateFn>(generic_iommu_translate_cb),
                                            m_int.get());
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    if (m_int->exports().cpu_riscv_register_mip_update_callback != nullptr) {
        m_int->exports().cpu_riscv_register_mip_update_callback(generic_cpu_riscv_mip_update_cb, m_int.get());
    }
}

void Cpu::set_pc_entry_callback(Cpu::PcEntryCallbackFn cb)
{
    m_int->exports().set_cpu_pc_entry_cb(generic_cpu_pc_entry_cb, m_int.get());
    m_int->get_cpu_pc_entry_cb().register_cb(*this, cb);
}

void Cpu::clear_pc_entry_callback()
{
    m_int->get_cpu_pc_entry_cb().clear(*this);
    m_int->exports().clear_cpu_pc_entry_watches();
    m_int->exports().set_cpu_pc_entry_cb(nullptr, nullptr);
}

void LibQemu::set_vm_state_callback(VmStateCallbackFn cb)
{
    const bool enabled = static_cast<bool>(cb);
    m_int->set_vm_state_callback(std::move(cb));
    m_int->exports().set_vm_state_cb(
        enabled ? generic_vm_state_cb : nullptr,
        enabled ? m_int.get() : nullptr);
}

} // namespace qemu
