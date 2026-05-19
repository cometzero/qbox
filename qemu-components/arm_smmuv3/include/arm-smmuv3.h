/*
 * This file is part of libqbox
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>

#include <cci_configuration>
#include <device.h>
#include <module_factory_registery.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/target.h>

class arm_smmuv3 : public QemuDevice
{
public:
    QemuTargetSocket<> mem;
    sc_core::sc_vector<QemuInitiatorSignalSocket> irq_out;
    cci::cci_param<std::string> p_stage;

    arm_smmuv3(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : arm_smmuv3(name, *(dynamic_cast<QemuInstance*>(o)), nullptr)
    {
    }

    arm_smmuv3(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* pci_host)
        : arm_smmuv3(name, *(dynamic_cast<QemuInstance*>(o)), dynamic_cast<QemuDevice*>(pci_host))
    {
    }

    arm_smmuv3(const sc_core::sc_module_name& name, QemuInstance& inst, QemuDevice* pci_host)
        : QemuDevice(name, inst, "arm-smmuv3")
        , mem("mem", inst)
        , irq_out("irq_out", 4, [](const char* n, int) { return new QemuInitiatorSignalSocket(n); })
        , p_stage("stage", "1", "SMMUv3 translation stage: 1, 2, or nested")
        , m_pci_host(pci_host)
    {
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();

        const std::string stage = p_stage.get_value();
        if (!stage.empty()) {
            m_dev.set_prop_str("stage", stage.c_str());
        }

        auto sysmem = this->get_qemu_inst().get().get_system_memory();
        m_dev.set_prop_link("memory", *sysmem);
        m_dev.set_prop_link("secure-memory", *sysmem);
    }

    void end_of_elaboration() override
    {
        if (m_pci_host) {
            qemu::Device pci_host = m_pci_host->get_qemu_dev();
            qemu::Bus primary_bus = pci_host.get_child_bus("pcie.0");

            if (!primary_bus.valid()) {
                SCP_FATAL(())("arm_smmuv3 primary PCI bus pcie.0 is not available");
            }
            m_dev.set_prop_bool("smmu_per_bus", true);
            m_dev.set_prop_link("primary-bus", primary_bus);
        }

        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        mem.init(sbd, 0);
        for (int i = 0; i < irq_out.size(); ++i) {
            irq_out[i].init_sbd(sbd, i);
        }
    }

private:
    QemuDevice* m_pci_host;
};

extern "C" void module_register();
