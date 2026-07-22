/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <systemc>

#include <device.h>
#include <module_factory_registery.h>
#include <ports/target-signal-socket.h>
#include <ports/target.h>
#include <qemu_clock_source.h>

class qemu_sse_counter : public QemuDevice
{
private:
    qemu_clock_source& m_clock_source;

public:
    QemuTargetSocket<> control;
    QemuTargetSocket<> status;
    TargetSignalSocket<bool> reset;

    qemu_sse_counter(const sc_core::sc_module_name& name,
                     sc_core::sc_object* instance,
                     sc_core::sc_object* clock_source);
    qemu_sse_counter(const sc_core::sc_module_name& name,
                     QemuInstance& instance,
                     qemu_clock_source& clock_source);

    const qemu::Device& qemu_device() const { return m_dev; }
    void before_end_of_elaboration() override;
    void end_of_elaboration() override;
};

extern "C" void module_register();
