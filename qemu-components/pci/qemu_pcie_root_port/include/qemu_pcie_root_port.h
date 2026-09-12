/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_PCIE_ROOT_PORT_H
#define _LIBQBOX_COMPONENTS_PCIE_ROOT_PORT_H

#include <cci_configuration>

#include <cstdint>
#include <string>
#include <vector>

#include <qemu_gpex.h>

class qemu_pcie_root_port : public qemu_gpex::Device
{
    cci::cci_param<std::string> p_addr;
    cci::cci_param<uint32_t> p_chassis;
    cci::cci_param<uint32_t> p_slot;
    cci::cci_param<uint32_t> p_port;
    cci::cci_param<std::string> p_x_speed;
    cci::cci_param<std::string> p_x_width;
    std::vector<qemu_gpex::Device*> devices;

public:
    qemu_pcie_root_port(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : qemu_pcie_root_port(name, *(dynamic_cast<QemuInstance*>(o)), dynamic_cast<qemu_gpex*>(t))
    {
    }

    qemu_pcie_root_port(const sc_core::sc_module_name& name, QemuInstance& inst, qemu_gpex* gpex)
        : qemu_gpex::Device(name, inst, "pcie-root-port", name)
        , p_addr("addr", "", "PCI slot on the GPEX root bus")
        , p_chassis("chassis", 0, "PCIe chassis number")
        , p_slot("slot", 0, "PCIe slot number")
        , p_port("port", 0, "PCIe port number")
        , p_x_speed("x_speed", "32", "Maximum PCIe link speed in GT/s")
        , p_x_width("x_width", "4", "Maximum PCIe link width")
        , devices()
    {
        gpex->add_device(*this);
    }

    void add_device(qemu_gpex::Device& dev)
    {
        if (m_inst != dev.get_qemu_inst()) {
            SCP_FATAL(SCMOD) << "PCIe device and root port have to be in the same QEMU instance";
        }
        devices.push_back(&dev);
    }

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();
        if (!p_addr.get_value().empty()) m_dev.set_prop_str("addr", p_addr.get_value().c_str());
        m_dev.set_prop_uint("chassis", p_chassis);
        m_dev.set_prop_uint("slot", p_slot);
        m_dev.set_prop_uint("port", p_port);
        m_dev.set_prop_parse("x-speed", p_x_speed.get_value().c_str());
        m_dev.set_prop_parse("x-width", p_x_width.get_value().c_str());
    }

protected:
    void gpex_realize(qemu::Bus& pcie_bus) override
    {
        qemu_gpex::Device::gpex_realize(pcie_bus);

        qemu::Bus downstream_bus = m_dev.get_child_bus(get_id());
        for (auto* dev : devices) {
            dev->gpex_realize(downstream_bus);
        }
    }
};

extern "C" void module_register();

#endif
