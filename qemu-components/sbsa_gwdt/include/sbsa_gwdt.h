/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_SBSA_GWDT_H
#define _LIBQBOX_COMPONENTS_SBSA_GWDT_H

#include <cci_configuration>
#include <device.h>
#include <module_factory_registery.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/target.h>
#include <qemu-instance.h>
#include <systemc>

class sbsa_gwdt : public QemuDevice
{
public:
    cci::cci_param<uint64_t> p_clock_frequency;
    QemuTargetSocket<> refresh_mem;
    QemuTargetSocket<> control_mem;
    QemuInitiatorSignalSocket irq_out;

    sbsa_gwdt(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : sbsa_gwdt(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    sbsa_gwdt(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "sbsa_gwdt")
        , p_clock_frequency("clock_frequency", 62500000, "Watchdog clock frequency in Hz")
        , refresh_mem("refresh_mem", inst)
        , control_mem("control_mem", inst)
        , irq_out("irq_out")
    {
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        m_dev.set_prop_uint("clock-frequency", p_clock_frequency.get_value());
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        refresh_mem.init(sbd, 0);
        control_mem.init(sbd, 1);
        irq_out.init_sbd(sbd, 0);
    }
};

extern "C" void module_register();

#endif
