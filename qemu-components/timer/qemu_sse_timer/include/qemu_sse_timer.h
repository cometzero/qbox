/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <systemc>

#include <device.h>
#include <module_factory_registery.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <ports/target.h>
#include <qemu_sse_counter.h>

class qemu_sse_timer : public QemuDevice
{
private:
    qemu_sse_counter& m_counter;

public:
    QemuTargetSocket<> socket;
    QemuInitiatorSignalSocket irq;
    TargetSignalSocket<bool> reset;

    qemu_sse_timer(const sc_core::sc_module_name& name,
                   sc_core::sc_object* instance,
                   sc_core::sc_object* counter);
    qemu_sse_timer(const sc_core::sc_module_name& name,
                   QemuInstance& instance, qemu_sse_counter& counter);

    qemu::ArmSSETimerSnapshot snapshot();
    bool shares_counter_with(const qemu_sse_timer& other) const
    {
        return &m_counter == &other.m_counter;
    }
    void before_end_of_elaboration() override;
    void end_of_elaboration() override;
};

extern "C" void module_register();
