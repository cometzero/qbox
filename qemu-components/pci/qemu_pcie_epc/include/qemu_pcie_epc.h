/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_PCIE_EPC_H
#define _LIBQBOX_COMPONENTS_PCIE_EPC_H

#include <cstdint>
#include <limits>

#include <cci_configuration>

#include <device.h>
#include <module_factory_registery.h>
#include <ports/initiator.h>
#include <ports/target.h>

class qemu_pcie_epc : public QemuDevice, public QemuInitiatorIface
{
    cci::cci_param<uint64_t> p_request_origin_id;
    cci::cci_param<uint32_t> p_request_domain_id;

public:
    QemuTargetSocket<> regs;
    QemuTargetSocket<> outbound;
    QemuInitiatorSocket<> local_master;

    qemu_pcie_epc(const sc_core::sc_module_name& name, sc_core::sc_object* obj)
        : qemu_pcie_epc(name, *dynamic_cast<QemuInstance*>(obj))
    {
    }

    qemu_pcie_epc(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "qbox-pcie-epc")
        , p_request_origin_id("request_origin_id", std::numeric_limits<uint64_t>::max(),
                              "Opaque request origin ID")
        , p_request_domain_id("request_domain_id", std::numeric_limits<uint32_t>::max(),
                              "Request domain ID")
        , regs("regs", inst)
        , outbound("outbound", inst)
        , local_master("local_master", *this, inst)
    {
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        local_master.set_request_context(make_request_context(
            p_request_origin_id, p_request_domain_id,
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max(), REQUEST_CONTEXT_CAP_NONE));
        local_master.init(m_dev, "local-memory");
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        regs.init(sbd, 0);
        outbound.init(sbd, 1);
    }

    sc_core::sc_time initiator_get_local_time() override
    {
        return sc_core::sc_time_stamp();
    }
    void initiator_set_local_time(const sc_core::sc_time&) override {}
    void initiator_customize_tlm_payload(TlmPayload&) override {}
    void initiator_tidy_tlm_payload(TlmPayload&) override {}
    void initiator_async_run(qemu::Cpu::AsyncJobFn) override {}
    void initiator_tlb_flush_all_cpus() override {}
};

extern "C" void module_register();

#endif
