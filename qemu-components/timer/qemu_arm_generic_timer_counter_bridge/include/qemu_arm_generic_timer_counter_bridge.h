/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <cci_configuration>
#include <systemc>

#include <arm_system_counter.h>
#include <libqemu-cxx/libqemu-cxx.h>
#include <module_factory_registery.h>
#include <qemu-instance.h>

class qemu_arm_generic_timer_counter_bridge : public sc_core::sc_module
{
public:
    struct EpochSnapshot {
        int64_t systemc_ns = 0;
        int64_t qemu_virtual_ns = 0;
        int64_t offset_ns = 0;
        uint64_t requested_generation = 0;
        uint64_t delivered_generation = 0;
    };

private:
    QemuInstance& m_inst;
    gs::arm_system_counter& m_counter;
    cci::cci_param<bool> p_freeze_mutations_on_start;
    qemu::ArmGenericTimerCounterProxy m_proxy;
    std::shared_ptr<qemu::IOThreadJob> m_notify_job;
    gs::arm_system_counter::ObserverSubscription m_subscription;
    std::atomic<int64_t> m_epoch_offset_ns{ 0 };
    std::atomic<uint64_t> m_requested_generation{ 0 };
    std::atomic<uint64_t> m_delivered_generation{ 0 };
    bool m_initialized = false;

    static uint64_t count_at_ns(void* opaque, int64_t qemu_ns);
    static bool deadline_ns(void* opaque, uint64_t target_count,
                            int64_t qemu_from_ns, int64_t* qemu_deadline_ns);
    static bool snapshot(
        void* opaque, int64_t qemu_ns,
        LibQemuArmGenericTimerCounterSnapshot* snapshot);
    int64_t systemc_ns_from_qemu(int64_t qemu_ns) const;
    int64_t qemu_ns_from_systemc(int64_t systemc_ns) const;
    void capture_epoch();
    void initialize();
    void request_notification(uint64_t generation);
    void notify_on_iothread();

public:
    qemu_arm_generic_timer_counter_bridge(
        const sc_core::sc_module_name& name, sc_core::sc_object* instance,
        sc_core::sc_object* counter);
    qemu_arm_generic_timer_counter_bridge(
        const sc_core::sc_module_name& name, QemuInstance& instance,
        gs::arm_system_counter& counter);
    ~qemu_arm_generic_timer_counter_bridge() override;

    const qemu::Object& counter_provider();
    EpochSnapshot epoch_snapshot() const;
    void drain_notifications();
    void disconnect();
    bool active() const { return m_initialized; }

    void before_end_of_elaboration() override;
    void start_of_simulation() override;
    void end_of_simulation() override;
};

extern "C" void module_register();
