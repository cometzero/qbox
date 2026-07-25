/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2015-2019
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <libqemu/libqemu.h>

#include <libqemu-cxx/target/aarch64.h>
#include "internals.h"

namespace qemu {

void CpuArm::set_cp15_cbar(uint64_t cbar) { m_int->exports().cpu_arm_set_cp15_cbar(m_obj, cbar); }

void CpuArm::set_imp_buildoptr(uint32_t imp_buildoptr)
{
    m_int->exports().cpu_arm_set_imp_buildoptr(m_obj, imp_buildoptr);
}

void CpuArm::add_nvic_link() { m_int->exports().cpu_arm_add_nvic_link(m_obj); }

uint64_t CpuArm::get_exclusive_addr() const { return m_int->exports().cpu_arm_get_exclusive_addr(m_obj); }

uint64_t CpuArm::get_exclusive_val() const { return m_int->exports().cpu_arm_get_exclusive_val(m_obj); }

void CpuArm::set_exclusive_val(uint64_t val) { m_int->exports().cpu_arm_set_exclusive_val(m_obj, val); }

void CpuArm::set_power_state(bool powered_on) { m_int->exports().cpu_arm_set_power_state(m_obj, powered_on); }

int CpuArm::power_on_and_reset() { return m_int->exports().cpu_arm_power_on_and_reset(m_obj); }

void CpuArm::set_gt_counter_mirror(bool active, bool running, uint64_t count,
                                   uint32_t frequency_hz,
                                   uint64_t generation)
{
    m_int->exports().cpu_arm_set_gt_counter_mirror(
        m_obj, active, running, count, frequency_hz, generation);
}

uint64_t CpuArm::get_gt_counter_value() const
{
    return m_int->exports().cpu_arm_get_gt_counter_value(m_obj);
}

uint64_t CpuArm::get_gt_counter_generation() const
{
    return m_int->exports().cpu_arm_get_gt_counter_generation(m_obj);
}

uint64_t CpuArm::get_v7m_state(V7MStateField field) const
{
    return m_int->exports().cpu_arm_v7m_get_state(m_obj, static_cast<int>(field));
}

bool CpuArm::set_v7m_state(V7MStateField field, uint64_t value)
{
    return m_int->exports().cpu_arm_v7m_set_state(m_obj, static_cast<int>(field), value);
}

void CpuArm::post_init() { m_int->exports().cpu_arm_post_init(m_obj); }

void CpuArm::register_reset() { m_int->exports().cpu_arm_register_reset(m_obj); }

int CpuArm::arm_set_cpu_on_and_reset() { return m_int->exports().cpu_arm_set_cpu_on_and_reset(m_obj); }

int CpuArm::arm_set_cpu_off() { return m_int->exports().cpu_arm_set_cpu_off(m_obj); }

void CpuAarch64::set_aarch64_mode(bool aarch64_mode)
{
    m_int->exports().cpu_aarch64_set_aarch64_mode(m_obj, aarch64_mode);
}

void ArmNvic::add_cpu_link() { m_int->exports().arm_nvic_add_cpu_link(m_obj); }

} // namespace qemu
