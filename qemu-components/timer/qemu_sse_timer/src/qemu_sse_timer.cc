/* SPDX-License-Identifier: BSD-3-Clause */

#include <qemu_sse_timer.h>

#include <stdexcept>

namespace {

template <typename T>
T& require_object(sc_core::sc_object* object, const char* type)
{
    T* typed = dynamic_cast<T*>(object);
    if (typed == nullptr) {
        throw std::invalid_argument(std::string("expected ") + type);
    }
    return *typed;
}

}

qemu_sse_timer::qemu_sse_timer(const sc_core::sc_module_name& name,
                               sc_core::sc_object* instance,
                               sc_core::sc_object* counter)
    : qemu_sse_timer(
          name, require_object<QemuInstance>(instance, "QemuInstance"),
          require_object<qemu_sse_counter>(counter, "qemu_sse_counter"))
{
}

qemu_sse_timer::qemu_sse_timer(const sc_core::sc_module_name& name,
                               QemuInstance& instance,
                               qemu_sse_counter& counter)
    : QemuDevice(name, instance, "sse-timer")
    , m_counter(counter)
    , socket("socket", instance)
    , irq("irq")
    , reset("reset")
{
    reset.register_value_changed_cb([this](bool asserted) {
        if (asserted) {
            m_inst.execute_on_iothread_sync(
                [this] { m_dev.cold_reset(); });
        }
    });
}

qemu::ArmSSETimerSnapshot qemu_sse_timer::snapshot()
{
    return m_inst.get().arm_sse_timer_snapshot(m_counter.qemu_device(), m_dev);
}

void qemu_sse_timer::before_end_of_elaboration()
{
    QemuDevice::before_end_of_elaboration();
    m_dev.set_prop_link("counter", m_counter.qemu_device());
}

void qemu_sse_timer::end_of_elaboration()
{
    QemuDevice::set_sysbus_as_parent_bus();
    QemuDevice::end_of_elaboration();
    qemu::SysBusDevice sysbus(m_dev);
    socket.init(sysbus, 0);
    irq.init_sbd(sysbus, 0);
}

void module_register()
{
    GSC_MODULE_REGISTER_C(qemu_sse_timer, sc_core::sc_object*,
                          sc_core::sc_object*);
}
