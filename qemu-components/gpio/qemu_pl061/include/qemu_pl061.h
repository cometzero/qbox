/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_QEMU_PL061_H
#define _LIBQBOX_COMPONENTS_QEMU_PL061_H

#include <cstddef>
#include <cstdint>

#include <cci_configuration>
#include <systemc>

#include <device.h>
#include <module_factory_registery.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <ports/target.h>

class qemu_pl061 : public QemuDevice
{
    static constexpr std::size_t num_gpios = 8;

    cci::cci_param<uint8_t> p_init_inputs;
    cci::cci_param<uint8_t> p_pullups;
    cci::cci_param<uint8_t> p_pulldowns;
    sc_core::sc_event m_reset_deasserted;
    bool m_gpio_ready = false;
    bool m_initial_inputs_pending = true;

    void validate_params() const
    {
        if ((p_pullups.get_value() & p_pulldowns.get_value()) != 0) {
            SCP_FATAL(SCMOD) << "pullups and pulldowns must not overlap";
        }
    }

    void apply_inputs()
    {
        if (!m_gpio_ready) {
            return;
        }

        if (m_initial_inputs_pending) {
            const uint8_t inputs = p_init_inputs.get_value();

            for (std::size_t i = 0; i < num_gpios; ++i) {
                gpio_in[i]->write((inputs & (1u << i)) != 0);
            }
            m_initial_inputs_pending = false;
            return;
        }

        for (std::size_t i = 0; i < num_gpios; ++i) {
            gpio_in[i]->write(gpio_in[i].read());
        }
    }

public:
    QemuTargetSocket<> mem;
    QemuInitiatorSignalSocket irq;
    sc_core::sc_vector<QemuTargetSignalSocket> gpio_in;
    sc_core::sc_vector<QemuInitiatorSignalSocket> gpio_out;
    TargetSignalSocket<bool> reset;

    qemu_pl061(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qemu_pl061(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qemu_pl061(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "pl061")
        , p_init_inputs("init_inputs", 0, "Initial GPIO input bitmap")
        , p_pullups("pullups", 0, "GPIO input pull-up bitmap")
        , p_pulldowns("pulldowns", 0, "GPIO input pull-down bitmap")
        , mem("mem", inst)
        , irq("irq")
        , gpio_in("gpio_in", num_gpios, [](const char* n, std::size_t) { return new QemuTargetSignalSocket(n); })
        , gpio_out("gpio_out", num_gpios, [](const char* n, std::size_t) { return new QemuInitiatorSignalSocket(n); })
        , reset("reset")
    {
        reset.register_value_changed_cb([this](const bool& asserted) {
            if (!asserted) {
                m_reset_deasserted.notify(sc_core::SC_ZERO_TIME);
            }
        });

        SC_METHOD(apply_inputs);
        sensitive << m_reset_deasserted;

        validate_params();
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        validate_params();
        m_dev.set_prop_uint("pullups", p_pullups.get_value());
        m_dev.set_prop_uint("pulldowns", p_pulldowns.get_value());
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        mem.init(sbd, 0);
        irq.init_sbd(sbd, 0);

        for (std::size_t i = 0; i < num_gpios; ++i) {
            gpio_in[i].init(m_dev, i);
            gpio_out[i].init(m_dev, i);
        }

        m_gpio_ready = true;
    }
};

extern "C" void module_register();

#endif
