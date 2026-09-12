/*
 * This file is part of libqbox
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cci_configuration>
#include <libqemu-cxx/target/aarch64.h>

#include <device.h>
#include <ports/target.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <module_factory_registery.h>

class nvic_armv7m : public QemuDevice
{
protected:
    static constexpr uint64_t SCS_SIZE = 0x1000;
    static constexpr uint64_t SYSTICK_OFFSET = 0x10;
    static constexpr uint64_t SYSTICK_SIZE = 0xe0;
    static constexpr uint64_t NS_ALIAS_OFFSET = 0x20000;
    static constexpr uint64_t DISPATCH_SIZE = NS_ALIAS_OFFSET + SCS_SIZE;

    bool before_end_of_elaboration_done;
    qemu::Device systick_ns;
    qemu::Device systick_s;
    qemu::Clock systick_cpuclk;
    qemu::MemoryRegion nvic_mr;
    qemu::MemoryRegion systick_ns_mr;
    qemu::MemoryRegion systick_s_mr;
    qemu::MemoryRegion dispatch_mr;
    std::shared_ptr<qemu::MemoryRegionOps> dispatch_ops;

    qemu::MemoryRegionOps::MemTxResult dispatch_read(
        uint64_t addr, uint64_t* data, unsigned int size,
        qemu::MemoryRegionOps::MemTxAttrs attrs)
    {
        if (addr >= NS_ALIAS_OFFSET && addr < DISPATCH_SIZE) {
            if (!p_systick_security || !attrs.secure) {
                if (attrs.user) return qemu::MemoryRegionOps::MemTxError;
                *data = 0;
                return qemu::MemoryRegionOps::MemTxOK;
            }
            addr -= NS_ALIAS_OFFSET;
            attrs.secure = false;
        } else if (addr >= SCS_SIZE) {
            if (attrs.user) return qemu::MemoryRegionOps::MemTxError;
            *data = 0;
            return qemu::MemoryRegionOps::MemTxOK;
        } else if (!p_systick_security) {
            attrs.secure = false;
        }

        if (addr >= SYSTICK_OFFSET &&
            addr < SYSTICK_OFFSET + SYSTICK_SIZE) {
            qemu::MemoryRegion& systick =
                p_systick_security && attrs.secure ? systick_s_mr : systick_ns_mr;
            return systick.dispatch_read(addr - SYSTICK_OFFSET, data, size,
                                         attrs);
        }
        return nvic_mr.dispatch_read(addr, data, size, attrs);
    }

    qemu::MemoryRegionOps::MemTxResult dispatch_write(
        uint64_t addr, uint64_t data, unsigned int size,
        qemu::MemoryRegionOps::MemTxAttrs attrs)
    {
        if (addr >= NS_ALIAS_OFFSET && addr < DISPATCH_SIZE) {
            if (!p_systick_security || !attrs.secure) {
                return attrs.user ? qemu::MemoryRegionOps::MemTxError
                                  : qemu::MemoryRegionOps::MemTxOK;
            }
            addr -= NS_ALIAS_OFFSET;
            attrs.secure = false;
        } else if (addr >= SCS_SIZE) {
            return attrs.user ? qemu::MemoryRegionOps::MemTxError
                              : qemu::MemoryRegionOps::MemTxOK;
        } else if (!p_systick_security) {
            attrs.secure = false;
        }

        if (addr >= SYSTICK_OFFSET &&
            addr < SYSTICK_OFFSET + SYSTICK_SIZE) {
            qemu::MemoryRegion& systick =
                p_systick_security && attrs.secure ? systick_s_mr : systick_ns_mr;
            return systick.dispatch_write(addr - SYSTICK_OFFSET, data, size,
                                          attrs);
        }
        return nvic_mr.dispatch_write(addr, data, size, attrs);
    }

public:
    cci::cci_param<uint32_t> p_num_irq;
    cci::cci_param<uint8_t> p_num_prio_bits;
    cci::cci_param<uint64_t> p_systick_cpuclk_hz;
    cci::cci_param<bool> p_systick_security;
    QemuTargetSocket<> socket;
    sc_core::sc_vector<QemuTargetSignalSocket> irq_in;
    QemuTargetSignalSocket nmi;
    QemuTargetSignalSocket NS_SysTick; // Non secure SysTick
    QemuTargetSignalSocket S_SysTick;  // Secure SysTick
    QemuInitiatorSignalSocket irq_out;

    nvic_armv7m(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : nvic_armv7m(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }
    nvic_armv7m(const sc_core::sc_module_name& n, QemuInstance& inst,
                bool systick_security = false)
        : QemuDevice(n, inst, "armv7m_nvic")
        , before_end_of_elaboration_done(false)
        , p_num_irq("num_irq", 64, "Number of external interrupts")
        , p_num_prio_bits("num_prio_bits", 0,
                          "Number of the maximum priority bits that can be used. 0 means to use a reasonable default")
        , p_systick_cpuclk_hz("systick_cpuclk_hz", 100000000,
                              "SysTick CPU clock frequency in Hz")
        , p_systick_security("systick_security", systick_security,
                             "Provide separate Secure and NonSecure SysTick banks")
        , socket("mem", inst)
        , irq_in("irq_in", p_num_irq)
        , nmi("nmi")
        , NS_SysTick("NS_SysTick")
        , S_SysTick("S_SysTick")
        , irq_out("irq_out")
    {
    }

    void before_end_of_elaboration() override
    {
        if (before_end_of_elaboration_done) {
            return;
        }

        QemuDevice::before_end_of_elaboration();
        before_end_of_elaboration_done = true;

        /* add the cpu link so we can set it later with set_cpu() */
        qemu::ArmNvic nvic(m_dev);
        nvic.add_cpu_link();

        m_dev.set_prop_int("num-irq", p_num_irq);
        m_dev.set_prop_int("num-prio-bits", p_num_prio_bits);

        systick_cpuclk = m_inst.get().clock_new(m_dev, "systick-cpuclk");
        if (p_systick_cpuclk_hz == 0 ||
            !systick_cpuclk.update_hz(p_systick_cpuclk_hz)) {
            throw qemu::LibQemuException("Invalid SysTick CPU clock frequency");
        }

        const std::string systick_ns_id =
            std::string(this->name()) + ".systick-ns";
        systick_ns = qemu::Device(m_inst.get().object_new(
            "armv7m_systick", systick_ns_id.c_str()));
        systick_ns.set_parent_bus(m_inst.get().sysbus_get_default());
        systick_ns.connect_clock_in("cpuclk", systick_cpuclk);

        if (p_systick_security) {
            const std::string systick_s_id =
                std::string(this->name()) + ".systick-s";
            systick_s = qemu::Device(m_inst.get().object_new(
                "armv7m_systick", systick_s_id.c_str()));
            systick_s.set_parent_bus(m_inst.get().sysbus_get_default());
            systick_s.connect_clock_in("cpuclk", systick_cpuclk);
        }
    }

    void end_of_elaboration() override
    {
        /*
         * At this point, the cpu link must have been set. Otherwise
         * realize will fail.
         *
         * Note: the cpu is used by qemu nvic to configure the nvic
         * depending on cpu features (See hw/intc/armv7m_nvic.c realize
         * function in qemu code).
         */
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);

        systick_ns.set_prop_bool("realized", true);
        qemu::SysBusDevice systick_ns_sbd(systick_ns);
        systick_ns_sbd.connect_gpio_out(
            0, m_dev.get_gpio_in_named("systick-trigger", 0));

        if (p_systick_security) {
            systick_s.set_prop_bool("realized", true);
            qemu::SysBusDevice(systick_s).connect_gpio_out(
                0, m_dev.get_gpio_in_named("systick-trigger", 1));
        }

        /* registers */
        nvic_mr = sbd.mmio_get_region(0);
        systick_ns_mr = systick_ns_sbd.mmio_get_region(0);
        if (p_systick_security) {
            systick_s_mr = qemu::SysBusDevice(systick_s).mmio_get_region(0);
        }

        dispatch_ops = m_inst.get().memory_region_ops_new();
        dispatch_ops->set_read_callback(
            [this](uint64_t addr, uint64_t* data, unsigned int size,
                   qemu::MemoryRegionOps::MemTxAttrs attrs) {
                return dispatch_read(addr, data, size, attrs);
            });
        dispatch_ops->set_write_callback(
            [this](uint64_t addr, uint64_t data, unsigned int size,
                   qemu::MemoryRegionOps::MemTxAttrs attrs) {
                return dispatch_write(addr, data, size, attrs);
            });
        dispatch_ops->set_max_access_size(4);
        dispatch_mr = m_inst.get().object_new_unparented<qemu::MemoryRegion>();
        dispatch_mr.init_io(m_dev, "armv7m-scs", DISPATCH_SIZE, dispatch_ops);
        socket.init_with_mr(dispatch_mr);

        /* interrupts */
        for (int i = 0; i < p_num_irq; i++) {
            irq_in[i].init(m_dev, i);
        }

        /* Output lines */
        irq_out.init_sbd(sbd, 0);
        nmi.init_named(m_dev, "NMI", 0);
        NS_SysTick.init_named(m_dev, "systick-trigger", 0);
        S_SysTick.init_named(m_dev, "systick-trigger", 1);
    }

    void cold_reset() override
    {
        qemu::Device(m_dev).cold_reset();
        systick_ns.cold_reset();
        if (p_systick_security) systick_s.cold_reset();
    }
};

extern "C" void module_register();
