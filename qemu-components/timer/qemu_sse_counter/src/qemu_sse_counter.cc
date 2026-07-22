/* SPDX-License-Identifier: BSD-3-Clause */

#include <qemu_sse_counter.h>

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

qemu_sse_counter::qemu_sse_counter(const sc_core::sc_module_name& name,
                                   sc_core::sc_object* instance,
                                   sc_core::sc_object* clock_source)
    : qemu_sse_counter(
          name, require_object<QemuInstance>(instance, "QemuInstance"),
          require_object<qemu_clock_source>(clock_source,
                                            "qemu_clock_source"))
{
}

qemu_sse_counter::qemu_sse_counter(const sc_core::sc_module_name& name,
                                   QemuInstance& instance,
                                   qemu_clock_source& clock_source)
    : QemuDevice(name, instance, "sse-counter")
    , m_clock_source(clock_source)
    , control("control", instance)
    , status("status", instance)
    , reset("reset")
{
    reset.register_value_changed_cb([this](bool asserted) {
        if (asserted) {
            m_inst.execute_on_iothread_sync(
                [this] { m_dev.cold_reset(); });
        }
    });
}

void qemu_sse_counter::before_end_of_elaboration()
{
    QemuDevice::before_end_of_elaboration();
    m_dev.connect_clock_in("CLK", m_clock_source.clock());
}

void qemu_sse_counter::end_of_elaboration()
{
    QemuDevice::set_sysbus_as_parent_bus();
    QemuDevice::end_of_elaboration();
    qemu::SysBusDevice sysbus(m_dev);
    control.init(sysbus, 0);
    status.init(sysbus, 1);
}

void module_register()
{
    GSC_MODULE_REGISTER_C(qemu_sse_counter, sc_core::sc_object*,
                          sc_core::sc_object*);
}
