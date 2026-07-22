/* SPDX-License-Identifier: BSD-3-Clause */

#include <qemu_arm_generic_timer_counter_bridge.h>

#include <exception>
#include <limits>
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

int64_t checked_add(int64_t value, int64_t delta)
{
    if ((delta > 0 && value > std::numeric_limits<int64_t>::max() - delta) ||
        (delta < 0 && value < std::numeric_limits<int64_t>::min() - delta)) {
        throw std::overflow_error("timer epoch conversion overflow");
    }
    return value + delta;
}

int64_t checked_subtract(int64_t value, int64_t subtrahend)
{
    if ((subtrahend > 0 &&
         value < std::numeric_limits<int64_t>::min() + subtrahend) ||
        (subtrahend < 0 &&
         value > std::numeric_limits<int64_t>::max() + subtrahend)) {
        throw std::overflow_error("timer epoch conversion overflow");
    }
    return value - subtrahend;
}

}

qemu_arm_generic_timer_counter_bridge::qemu_arm_generic_timer_counter_bridge(
    const sc_core::sc_module_name& name, sc_core::sc_object* instance,
    sc_core::sc_object* counter)
    : qemu_arm_generic_timer_counter_bridge(
          name, require_object<QemuInstance>(instance, "QemuInstance"),
          require_object<gs::arm_system_counter>(counter,
                                                 "arm_system_counter"))
{
}

qemu_arm_generic_timer_counter_bridge::qemu_arm_generic_timer_counter_bridge(
    const sc_core::sc_module_name& name, QemuInstance& instance,
    gs::arm_system_counter& counter)
    : sc_core::sc_module(name)
    , m_inst(instance)
    , m_counter(counter)
    , p_freeze_mutations_on_start(
          "freeze_mutations_on_start", true,
          "Freeze shared counter rate and scale after simulation starts")
{
}

qemu_arm_generic_timer_counter_bridge::~qemu_arm_generic_timer_counter_bridge()
{
    disconnect();
}

uint64_t qemu_arm_generic_timer_counter_bridge::count_at_ns(
    void* opaque, int64_t qemu_ns)
{
    qemu_arm_generic_timer_counter_bridge* bridge =
        static_cast<qemu_arm_generic_timer_counter_bridge*>(opaque);
    try {
        return bridge->m_counter.count_at(
            bridge->systemc_ns_from_qemu(qemu_ns));
    } catch (...) {
        std::terminate();
    }
}

bool qemu_arm_generic_timer_counter_bridge::deadline_ns(
    void* opaque, uint64_t target_count, int64_t qemu_from_ns,
    int64_t* qemu_deadline_ns)
{
    qemu_arm_generic_timer_counter_bridge* bridge =
        static_cast<qemu_arm_generic_timer_counter_bridge*>(opaque);
    try {
        int64_t systemc_deadline_ns;
        if (!bridge->m_counter.deadline_ns(
                target_count, bridge->systemc_ns_from_qemu(qemu_from_ns),
                systemc_deadline_ns)) {
            return false;
        }
        *qemu_deadline_ns = bridge->qemu_ns_from_systemc(systemc_deadline_ns);
        return true;
    } catch (...) {
        std::terminate();
    }
}

bool qemu_arm_generic_timer_counter_bridge::snapshot(
    void* opaque, int64_t qemu_ns,
    LibQemuArmGenericTimerCounterSnapshot* snapshot)
{
    if (snapshot == nullptr || snapshot->size < sizeof(*snapshot) ||
        snapshot->version !=
            LIBQEMU_ARM_GENERIC_TIMER_COUNTER_SNAPSHOT_ABI) {
        return false;
    }
    qemu_arm_generic_timer_counter_bridge* bridge =
        static_cast<qemu_arm_generic_timer_counter_bridge*>(opaque);
    try {
        const gs::arm_system_counter::StateSnapshot state =
            bridge->m_counter.snapshot_at(
                bridge->systemc_ns_from_qemu(qemu_ns));
        snapshot->qemu_virtual_ns = qemu_ns;
        snapshot->count = state.anchor_count;
        snapshot->nominal_frequency_hz = state.input_frequency_hz;
        snapshot->reported_frequency_hz = state.reported_frequency_hz;
        snapshot->enabled = state.enabled;
        snapshot->halted = state.halt_on_debug && state.debug_halted;
        return true;
    } catch (...) {
        return false;
    }
}

int64_t qemu_arm_generic_timer_counter_bridge::systemc_ns_from_qemu(
    int64_t qemu_ns) const
{
    return checked_add(qemu_ns,
                       m_epoch_offset_ns.load(std::memory_order_acquire));
}

int64_t qemu_arm_generic_timer_counter_bridge::qemu_ns_from_systemc(
    int64_t systemc_ns) const
{
    return checked_subtract(
        systemc_ns, m_epoch_offset_ns.load(std::memory_order_acquire));
}

void qemu_arm_generic_timer_counter_bridge::capture_epoch()
{
    qemu::LibQemu& qemu = m_inst.get();
    const int64_t systemc_ns = gs::arm_system_counter::absolute_ns(
        sc_core::sc_time_stamp());
    const int64_t qemu_ns = qemu.get_virtual_clock();
    m_epoch_offset_ns.store(checked_subtract(systemc_ns, qemu_ns),
                            std::memory_order_release);
}

void qemu_arm_generic_timer_counter_bridge::initialize()
{
    if (m_initialized) {
        return;
    }
    qemu::LibQemu& qemu = m_inst.get();
    capture_epoch();

    LibQemuArmGenericTimerCounterCallbacks callbacks = {};
    callbacks.size = sizeof(callbacks);
    callbacks.version = LIBQEMU_ARM_GENERIC_TIMER_COUNTER_ABI;
    callbacks.count_at_ns = &qemu_arm_generic_timer_counter_bridge::count_at_ns;
    callbacks.deadline_ns = &qemu_arm_generic_timer_counter_bridge::deadline_ns;
    callbacks.snapshot = &qemu_arm_generic_timer_counter_bridge::snapshot;
    m_proxy = qemu.arm_generic_timer_counter_proxy_new(callbacks, this);
    m_notify_job = qemu.iothread_job_new(
        [this] { notify_on_iothread(); });
    m_subscription = m_counter.observe(
        [this](uint64_t generation) { request_notification(generation); });
    m_initialized = true;
}

void qemu_arm_generic_timer_counter_bridge::disconnect()
{
    if (!m_initialized) {
        return;
    }
    m_subscription.reset();
    m_notify_job->stop();
    m_notify_job->drain();
    m_proxy.clear();
    m_notify_job.reset();
    m_proxy = qemu::ArmGenericTimerCounterProxy();
    m_initialized = false;
}

void qemu_arm_generic_timer_counter_bridge::request_notification(
    uint64_t generation)
{
    m_requested_generation.store(generation, std::memory_order_release);
    m_notify_job->schedule();
}

void qemu_arm_generic_timer_counter_bridge::notify_on_iothread()
{
    const uint64_t generation =
        m_requested_generation.load(std::memory_order_acquire);
    m_proxy.notify();
    m_delivered_generation.store(generation, std::memory_order_release);
}

const qemu::Object&
qemu_arm_generic_timer_counter_bridge::counter_provider()
{
    initialize();
    return m_proxy;
}

qemu_arm_generic_timer_counter_bridge::EpochSnapshot
qemu_arm_generic_timer_counter_bridge::epoch_snapshot() const
{
    EpochSnapshot snapshot;
    snapshot.systemc_ns = gs::arm_system_counter::absolute_ns(
        sc_core::sc_time_stamp());
    snapshot.qemu_virtual_ns = m_inst.get().get_virtual_clock();
    snapshot.offset_ns = m_epoch_offset_ns.load(std::memory_order_acquire);
    snapshot.requested_generation =
        m_requested_generation.load(std::memory_order_acquire);
    snapshot.delivered_generation =
        m_delivered_generation.load(std::memory_order_acquire);
    return snapshot;
}

void qemu_arm_generic_timer_counter_bridge::drain_notifications()
{
    if (m_notify_job) {
        m_notify_job->drain();
        m_notify_job->rethrow_if_failed();
    }
}

void qemu_arm_generic_timer_counter_bridge::before_end_of_elaboration()
{
    initialize();
}

void qemu_arm_generic_timer_counter_bridge::start_of_simulation()
{
    capture_epoch();
    request_notification(m_counter.snapshot().generation);
    drain_notifications();
    if (p_freeze_mutations_on_start.get_value()) {
        m_counter.freeze_mutations();
    }
}

void qemu_arm_generic_timer_counter_bridge::end_of_simulation()
{
    disconnect();
}

void module_register()
{
    GSC_MODULE_REGISTER_C(qemu_arm_generic_timer_counter_bridge,
                          sc_core::sc_object*, sc_core::sc_object*);
}
