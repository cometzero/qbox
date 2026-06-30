/*
 * This file is part of libqbox
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_ARM_GICV3_ITS_H
#define _LIBQBOX_COMPONENTS_ARM_GICV3_ITS_H

#include <module_factory_registery.h>
#include "arm_gicv3.h"
#include <string>
#include <sstream>

class arm_gicv3_its : public QemuDevice
{
public:
    QemuTargetSocket<> mem;

private:
    arm_gicv3* m_parent_gicv3;
    cci::cci_param<bool> p_has_gicv4_1;
    cci::cci_param<unsigned int> p_gicv4_1_svpet;
    cci::cci_param<unsigned int> p_gicv4_1_cte_size;

public:
    arm_gicv3_its(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : arm_gicv3_its(name, *(dynamic_cast<QemuInstance*>(o)), dynamic_cast<arm_gicv3*>(t))
    {
    }
    arm_gicv3_its(const sc_core::sc_module_name& n, QemuInstance& inst, arm_gicv3* _arm_gicv3)
        : QemuDevice(n, inst, "arm-gicv3-its")
        , m_parent_gicv3(_arm_gicv3)
        , p_has_gicv4_1("has_gicv4_1", false, "Expose GICv4.1 ITS feature discovery")
        , p_gicv4_1_svpet("gicv4_1_svpet", 0, "GICv4.1 SVPET value")
        , p_gicv4_1_cte_size("gicv4_1_cte_size", 8, "GICv4.1 collection table entry size")
        , mem("mem", inst)

    {
    }

    void before_end_of_elaboration()
    {
        QemuDevice::before_end_of_elaboration();

        m_dev.set_prop_link("parent-gicv3", m_parent_gicv3->get_qemu_dev());
        m_dev.set_prop_bool("has-gicv4-1", p_has_gicv4_1);
        m_dev.set_prop_int("gicv4-1-svpet", p_gicv4_1_svpet);
        m_dev.set_prop_int("gicv4-1-cte-size", p_gicv4_1_cte_size);
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();
        qemu::SysBusDevice sbd(m_dev);
        mem.init(sbd, 0);
    }
};

extern "C" void module_register();

#endif
