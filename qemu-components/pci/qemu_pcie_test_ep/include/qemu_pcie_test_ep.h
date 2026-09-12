/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_PCIE_TEST_EP_H
#define _LIBQBOX_COMPONENTS_PCIE_TEST_EP_H

#include <cci_configuration>

#include <module_factory_registery.h>
#include <qemu_pcie_epc.h>
#include <qemu_pcie_root_port.h>

class qemu_pcie_test_ep : public qemu_gpex::Device
{
    cci::cci_param<std::string> p_addr;
    qemu_pcie_epc& m_epc;

public:
    qemu_pcie_test_ep(const sc_core::sc_module_name& name,
                      sc_core::sc_object* inst,
                      sc_core::sc_object* root_port,
                      sc_core::sc_object* epc)
        : qemu_pcie_test_ep(name, *dynamic_cast<QemuInstance*>(inst),
                            *dynamic_cast<qemu_pcie_root_port*>(root_port),
                            *dynamic_cast<qemu_pcie_epc*>(epc))
    {
    }

    qemu_pcie_test_ep(const sc_core::sc_module_name& name,
                      QemuInstance& inst,
                      qemu_pcie_root_port& root_port,
                      qemu_pcie_epc& epc)
        : qemu_gpex::Device(name, inst, "qbox-pcie-test-ep", name)
        , p_addr("addr", "00.0", "PCI function address on the root port")
        , m_epc(epc)
    {
        if (&inst != &epc.get_qemu_inst()) {
            SCP_FATAL(SCMOD) << "PCIe EPC and endpoint must use the same QEMU instance";
        }
        root_port.add_device(*this);
    }

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();
        m_epc.instantiate();
        m_dev.set_prop_link("epc", m_epc.get_qemu_dev());
        m_dev.set_prop_str("addr", p_addr.get_value().c_str());
    }
};

extern "C" void module_register();

#endif
