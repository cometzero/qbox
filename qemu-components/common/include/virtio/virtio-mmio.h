/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdlib>
#include <cstring>
#include <vector>

#include <cci_configuration>

#include <device.h>
#include <ports/target.h>
#include <ports/qemu-initiator-signal-socket.h>

#include <cciutils.h>
#include <scp/report.h>

class QemuVirtioMMIO : public QemuDevice
{
    SCP_LOGGER();

    static bool env_flag_enabled(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            return false;
        }

        return std::strcmp(value, "1") == 0 ||
               std::strcmp(value, "true") == 0 ||
               std::strcmp(value, "yes") == 0 ||
               std::strcmp(value, "on") == 0;
    }

    static std::string env_string_or(const char* name, const std::string& fallback)
    {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            return fallback;
        }

        return std::string(value);
    }

    static uint64_t env_uint_or(const char* name, uint64_t fallback)
    {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            return fallback;
        }

        char* end = nullptr;
        const uint64_t parsed = std::strtoull(value, &end, 0);
        if (end == value || *end != '\0') {
            return fallback;
        }

        return parsed;
    }

public:
    QemuTargetSocket<> socket;
    QemuInitiatorSignalSocket irq_out;
    QemuDevice virtio_mmio_device;
    cci::cci_param<bool> p_trace;
    cci::cci_param<unsigned int> p_trace_limit;
    cci::cci_param<std::string> p_trace_file;
    cci::cci_param<std::string> p_trace_filter;
    cci::cci_param<bool> p_ioeventfd;

    /*
     * qemu qbus hierachy created behind this sc_module:
     * virtio_mmio_device
     *   + qemu-type: "virtio-mmio" (this is a sysbus device)
     *   + qbus-child(&qon-child) "virtio-mmio-bus":
     *       + qdev-child (this QemuVirtioMMIO object):
     *       + qemu-type: "virtio-net-device"
     */
    QemuVirtioMMIO(sc_core::sc_module_name nm, QemuInstance& inst, const char* device_type)
        : QemuDevice(nm, inst, device_type)
        , socket("mem", inst)
        , irq_out("irq_out")
        , virtio_mmio_device("virtio_mmio", inst, "virtio-mmio")
        , p_trace("trace", false)
        , p_trace_limit("trace_limit", 256)
        , p_trace_file("trace_file", std::string(""))
        , p_trace_filter("trace_filter", "control")
        , p_ioeventfd("ioeventfd", false)
    {
        SCP_TRACE(())("Constructor");
    }

    void before_end_of_elaboration() override
    {
        virtio_mmio_device.instantiate();
        QemuDevice::before_end_of_elaboration();
        for (auto n : gs::sc_cci_children((std::string(name()) + ".device_properties").c_str())) {
            cci::cci_value v = gs::cci_get(cci::cci_get_broker(), std::string(name()) + ".device_properties." + n);
            /* NB we could use _parse here,but JSON strings dont always match QEMU command line strings! */
            if (v.is_bool()) {
                SCP_DEBUG(())("Setting bool {} to {}", n, v.to_json());
                m_dev.set_prop_bool(n.c_str(), v.get_bool());
                continue;
            }
            if (v.is_number()) {
                SCP_DEBUG(())("Setting number {} to {}", n, v.to_json());
                m_dev.set_prop_int(n.c_str(), v.get_uint64());
                continue;
            }
            if (v.is_string()) {
                SCP_DEBUG(())("Setting string {} to {}", n, v.to_json());
                m_dev.set_prop_str(n.c_str(), v.get_string().c_str());
                continue;
            }
            SCP_WARN(())("Ignoring property {}, unknown type. {}", n, v.to_json());
        }

        virtio_mmio_device.get_qemu_dev().set_prop_bool("force-legacy", true);
        virtio_mmio_device.get_qemu_dev().set_prop_bool("ioeventfd", p_ioeventfd.get_value());
    }

    void end_of_elaboration() override
    {
        /*
         * we realize virtio_mmio_device first because
         * it creates the "virtio-mmio-bus" we need below
         */
        virtio_mmio_device.set_sysbus_as_parent_bus();
        virtio_mmio_device.realize();

        /*
         * Expose the sysbus device mmio & irq
         */
        qemu::SysBusDevice sbd(virtio_mmio_device.get_qemu_dev());
        socket.init(sbd, 0);
        socket.set_trace(name(),
                         p_trace.get_value() || env_flag_enabled("QBOX_VIRTIO_MMIO_TRACE"),
                         env_string_or("QBOX_VIRTIO_MMIO_TRACE_FILE", p_trace_file.get_value()),
                         env_uint_or("QBOX_VIRTIO_MMIO_TRACE_LIMIT", p_trace_limit.get_value()),
                         env_string_or("QBOX_VIRTIO_MMIO_TRACE_FILTER", p_trace_filter.get_value()));
        irq_out.init_sbd(sbd, 0);

        /*
         * Realize the true virtio net object
         */
        qemu::Device virtio_device(m_dev);
        virtio_device.set_parent_bus(QemuVirtioMMIO::get_bus());
        QemuDevice::end_of_elaboration();
    }

private:
    qemu::Bus get_bus()
    {
        qemu::Device virtio_mmio_dev(virtio_mmio_device.get_qemu_dev());
        return virtio_mmio_dev.get_prop_link("virtio-mmio-bus");
    }
};
