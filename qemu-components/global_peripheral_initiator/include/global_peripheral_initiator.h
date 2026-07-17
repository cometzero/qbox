/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_GOLBAL_PERIPHERAL_INITIATOR_H
#define _LIBQBOX_COMPONENTS_GOLBAL_PERIPHERAL_INITIATOR_H

#include <limits>

#include <cci_configuration>

#include <ports/initiator.h>
#include <device.h>
#include <qemu-instance.h>
#include <module_factory_registery.h>

class global_peripheral_initiator : public QemuInitiatorIface, public sc_core::sc_module
{
public:
    // QemuInitiatorIface functions
    using TlmPayload = tlm::tlm_generic_payload;
    virtual void initiator_customize_tlm_payload(TlmPayload& payload) override {}
    virtual void initiator_tidy_tlm_payload(TlmPayload& payload) override {}
    virtual sc_core::sc_time initiator_get_local_time() override { return sc_core::sc_time_stamp(); }
    virtual void initiator_set_local_time(const sc_core::sc_time&) override {}

    cci::cci_param<uint64_t> p_request_origin_id;
    cci::cci_param<uint32_t> p_request_domain_id;
    cci::cci_param<uint32_t> p_requester_id;
    cci::cci_param<uint32_t> p_request_substream_id;
    cci::cci_param<uint32_t> p_request_capabilities;
    QemuInitiatorSocket<> m_initiator;
    global_peripheral_initiator(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : global_peripheral_initiator(name, *(dynamic_cast<QemuInstance*>(o)), *(dynamic_cast<QemuDevice*>(t)))
    {
    }
    global_peripheral_initiator(const sc_core::sc_module_name& nm, QemuInstance& inst, QemuDevice& owner)
        : p_request_origin_id("request_origin_id", std::numeric_limits<uint64_t>::max(), "Opaque request origin ID")
        , p_request_domain_id("request_domain_id", std::numeric_limits<uint32_t>::max(), "Request domain ID")
        , p_requester_id("requester_id", std::numeric_limits<uint32_t>::max(), "Request SID or requester ID")
        , p_request_substream_id("request_substream_id", std::numeric_limits<uint32_t>::max(), "Request SSID")
        , p_request_capabilities("request_capabilities", REQUEST_CONTEXT_CAP_NONE, "Request capability flags")
        , m_initiator("global_initiator", *this, inst)
        , m_owner(owner)
    {
    }

    virtual void before_end_of_elaboration() override
    {
        qemu::Device dev = m_owner.get_qemu_dev();
        m_initiator.set_request_context(make_request_context(
            p_request_origin_id, p_request_domain_id, p_requester_id,
            p_request_substream_id, p_request_capabilities));
        m_initiator.init_global(dev);
    }

    virtual void initiator_async_run(qemu::Cpu::AsyncJobFn job) override {}

private:
    QemuDevice& m_owner;
};

extern "C" void module_register();
#endif //_LIBQBOX_COMPONENTS_GOLBAL_PERIPHERAL_INITIATOR_H
