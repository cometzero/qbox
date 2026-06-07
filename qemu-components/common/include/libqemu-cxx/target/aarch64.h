/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2015-2019
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <libqemu-cxx/libqemu-cxx.h>

namespace qemu {

class CpuArm : public Cpu
{
public:
    static constexpr const char* const TYPE = "arm-cpu";

    enum class V7MStateField {
        R0 = 0,
        R1,
        R2,
        R3,
        R4,
        R5,
        R6,
        R7,
        R8,
        R9,
        R10,
        R11,
        R12,
        SP,
        LR,
        PC,
        XPSR,
        EXCEPTION,
        CPU_EXCEPTION_INDEX,
        SECURE,
        CFSR_NS,
        CFSR_S,
        HFSR,
        DFSR,
        SFSR,
        MMFAR_NS,
        MMFAR_S,
        BFAR,
        SFAR,
        AIRCR,
        VTOR_NS,
        VTOR_S,
        CONTROL_NS,
        CONTROL_S,
        PRIMASK_NS,
        PRIMASK_S,
        FAULTMASK_NS,
        FAULTMASK_S,
        BASEPRI_NS,
        BASEPRI_S,
        OTHER_SP,
        OTHER_SS_MSP,
        OTHER_SS_PSP,
        MSPLIM_NS,
        MSPLIM_S,
        PSPLIM_NS,
        PSPLIM_S,
    };

    enum class Aarch64StateField {
        IS_A64 = 0,
        PC,
        SP,
        LR,
        PSTATE,
        CURRENT_EL,
        CPU_EXCEPTION_INDEX,
        X0,
        X1,
        X2,
        X3,
        X4,
        X5,
        X6,
        X7,
        X29,
        EXCEPTION_SYNDROME,
        EXCEPTION_VADDRESS,
        ESR_EL3,
        FAR_EL3,
        ELR_EL3,
        SP_EL0,
        SP_EL3,
    };

    CpuArm() = default;
    CpuArm(const CpuArm&) = default;
    CpuArm(const Object& o): Cpu(o) {}

    void set_cp15_cbar(uint64_t cbar);
    void add_nvic_link();

    uint64_t get_exclusive_addr() const;
    uint64_t get_exclusive_val() const;
    void set_exclusive_val(uint64_t val);
    void set_power_state(bool powered_on);
    int get_power_state() const;
    int power_on_and_reset();
    uint64_t get_v7m_state(V7MStateField field) const;
    bool set_v7m_state(V7MStateField field, uint64_t value);
    uint64_t get_aarch64_state(Aarch64StateField field) const;

    void post_init();
    void register_reset();
};

class CpuAarch64 : public CpuArm
{
public:
    static constexpr const char* const TYPE = "arm-cpu";

    CpuAarch64() = default;
    CpuAarch64(const CpuAarch64&) = default;
    CpuAarch64(const Object& o): CpuArm(o) {}

    void set_aarch64_mode(bool aarch64_mode);
};

class ArmNvic : public Device
{
public:
    static constexpr const char* const TYPE = "armv7m_nvic";

    ArmNvic() = default;
    ArmNvic(const ArmNvic&) = default;
    ArmNvic(const Object& o): Device(o) {}

    void add_cpu_link();
};

} // namespace qemu
