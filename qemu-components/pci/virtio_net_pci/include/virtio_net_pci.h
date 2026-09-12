/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_VIRTIO_NET_PCI_H
#define _LIBQBOX_COMPONENTS_VIRTIO_NET_PCI_H

#include <cci_configuration>

#include <libgssync.h>
#include <qemu-instance.h>

#include <module_factory_registery.h>

#include <qemu_gpex.h>
#include <qemu_pcie_root_port.h>

class virtio_net_pci : public qemu_gpex::Device
{
    std::string m_netdev_id;
    cci::cci_param<std::string> p_mac;
    cci::cci_param<std::string> p_netdev_str;
    cci::cci_param<std::string> p_addr;
    cci::cci_param<bool> p_iommu_platform;

public:
    virtio_net_pci(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : virtio_net_pci(name, *(dynamic_cast<QemuInstance*>(o)), t)
    {
    }
    virtio_net_pci(const sc_core::sc_module_name& name, QemuInstance& inst, sc_core::sc_object* bus)
        : qemu_gpex::Device(name, inst, "virtio-net-pci")
        , m_netdev_id(std::string(sc_core::sc_module::name()) + "-id")
        , p_mac("mac", "", "MAC address of NIC")
        , p_netdev_str("netdev_str", "type=user", "netdev string for QEMU (do not specify ID)")
        , p_addr("addr", "", "PCI slot pinning, e.g. \"01.0\"; empty leaves it to gpex auto-assignment")
        , p_iommu_platform("iommu_platform", false, "Use the PCI DMA address space")
    {
        std::stringstream opts;
        opts << p_netdev_str.get_value();
        opts << ",id=" << m_netdev_id;

        m_inst.add_arg("-netdev");
        m_inst.add_arg(opts.str().c_str());

        if (auto* gpex = dynamic_cast<qemu_gpex*>(bus)) {
            gpex->add_device(*this);
        } else if (auto* root_port = dynamic_cast<qemu_pcie_root_port*>(bus)) {
            root_port->add_device(*this);
        } else {
            SCP_FATAL(SCMOD) << "virtio-net-pci requires a GPEX or PCIe root port parent";
        }
    }

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();
        if (!p_mac.get_value().empty()) m_dev.set_prop_str("mac", p_mac.get_value().c_str());
        m_dev.set_prop_str("netdev", m_netdev_id.c_str());
        m_dev.set_prop_str("romfile", "");
        if (!p_addr.get_value().empty()) m_dev.set_prop_str("addr", p_addr.get_value().c_str());
        m_dev.set_prop_bool("iommu_platform", p_iommu_platform);
    }
};

extern "C" void module_register();

#endif
