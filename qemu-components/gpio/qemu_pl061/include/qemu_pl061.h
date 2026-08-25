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
#include <tlm>

#include <device.h>
#include <module_factory_registery.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <ports/target.h>

class qemu_pl061 : public QemuDevice
{
    static constexpr std::size_t num_gpios = 8;
    static constexpr uint64_t data_register = 0x3fc;
    static constexpr uint64_t direction_register = 0x400;

    cci::cci_param<uint8_t> p_init_inputs;
    cci::cci_param<uint8_t> p_pullups;
    cci::cci_param<uint8_t> p_pulldowns;
    sc_core::sc_event m_reset_deasserted;
    bool m_gpio_ready = false;
    bool m_initial_inputs_pending = true;

    bool runtime_read_register(uint64_t address, uint32_t& value)
    {
        if (!m_gpio_ready) {
            return false;
        }
        tlm::tlm_generic_payload payload;
        payload.set_command(tlm::TLM_READ_COMMAND);
        payload.set_address(address);
        payload.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        payload.set_data_length(sizeof(value));
        payload.set_streaming_width(sizeof(value));
        payload.set_byte_enable_length(0);
        payload.set_dmi_allowed(false);
        payload.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return mem.transport_dbg(payload) == sizeof(value) && payload.get_response_status() == tlm::TLM_OK_RESPONSE;
    }

    bool runtime_write_register(uint64_t address, uint32_t value)
    {
        if (!m_gpio_ready) {
            return false;
        }
        tlm::tlm_generic_payload payload;
        payload.set_command(tlm::TLM_WRITE_COMMAND);
        payload.set_address(address);
        payload.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        payload.set_data_length(sizeof(value));
        payload.set_streaming_width(sizeof(value));
        payload.set_byte_enable_length(0);
        payload.set_dmi_allowed(false);
        payload.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return mem.transport_dbg(payload) == sizeof(value) && payload.get_response_status() == tlm::TLM_OK_RESPONSE;
    }

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
    struct RuntimePinSnapshot {
        bool direction_output = false;
        bool data_level = false;
        bool input_level = false;
        bool initial_input_level = false;
    };

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

    bool runtime_pin_snapshot(std::size_t pin, RuntimePinSnapshot& snapshot)
    {
        if (pin >= num_gpios || !m_gpio_ready) {
            return false;
        }
        uint32_t direction = 0;
        uint32_t data = 0;
        if (!runtime_read_register(direction_register, direction) || !runtime_read_register(data_register, data)) {
            return false;
        }
        const uint32_t mask = 1u << pin;
        snapshot.direction_output = (direction & mask) != 0;
        snapshot.data_level = (data & mask) != 0;
        snapshot.input_level = gpio_in[pin].read();
        snapshot.initial_input_level = (p_init_inputs.get_value() & mask) != 0;
        return true;
    }

    bool runtime_set_direction(std::size_t pin, bool output)
    {
        if (pin >= num_gpios || !m_gpio_ready) {
            return false;
        }
        uint32_t direction = 0;
        if (!runtime_read_register(direction_register, direction)) {
            return false;
        }
        const uint32_t mask = 1u << pin;
        direction = output ? (direction | mask) : (direction & ~mask);
        if (!runtime_write_register(direction_register, direction)) {
            return false;
        }
        uint32_t readback = 0;
        return runtime_read_register(direction_register, readback) && ((readback & mask) != 0) == output;
    }

    bool runtime_write_output(std::size_t pin, bool level)
    {
        if (pin >= num_gpios || !m_gpio_ready) {
            return false;
        }
        RuntimePinSnapshot snapshot;
        if (!runtime_pin_snapshot(pin, snapshot) || !snapshot.direction_output) {
            return false;
        }
        const uint32_t mask = 1u << pin;
        if (!runtime_write_register(mask << 2, level ? mask : 0)) {
            return false;
        }
        return runtime_pin_snapshot(pin, snapshot) && snapshot.data_level == level;
    }

    bool runtime_drive_input(std::size_t pin, bool level)
    {
        if (pin >= num_gpios || !m_gpio_ready) {
            return false;
        }
        RuntimePinSnapshot snapshot;
        if (!runtime_pin_snapshot(pin, snapshot) || snapshot.direction_output) {
            return false;
        }
        gpio_in[pin]->write(level);
        return runtime_pin_snapshot(pin, snapshot) && snapshot.data_level == level;
    }

    bool runtime_release_input(std::size_t pin)
    {
        if (pin >= num_gpios || !m_gpio_ready) {
            return false;
        }
        return runtime_drive_input(pin, (p_init_inputs.get_value() & (1u << pin)) != 0);
    }
};

extern "C" void module_register();

#endif
