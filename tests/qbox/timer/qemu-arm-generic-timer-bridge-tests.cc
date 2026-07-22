/* SPDX-License-Identifier: BSD-3-Clause */

#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include <cci/utils/broker.h>
#include <systemc>

#include <arm_system_counter.h>
#include <qemu-instance.h>
#include <qemu_arm_arch_timer_mmio.h>
#include <qemu_arm_generic_timer_counter_bridge.h>
#include <qemu_sse_timer.h>

namespace {

class TwoInstanceTimerBench : public sc_core::sc_module
{
public:
    QemuInstanceManager manager;
    QemuInstance instance0;
    QemuInstance instance1;
    gs::arm_system_counter counter;
    qemu_arm_generic_timer_counter_bridge bridge0;
    qemu_arm_generic_timer_counter_bridge bridge1;
    qemu_arm_arch_timer_mmio_external_counter timer0;
    qemu_arm_arch_timer_mmio_external_counter timer1;
    qemu_clock_source sse_clock;
    qemu_sse_counter sse_counter;
    qemu_sse_timer sse_timer;

    explicit TwoInstanceTimerBench(const sc_core::sc_module_name& name)
        : sc_core::sc_module(name)
        , manager("manager")
        , instance0("instance0", &manager, QemuInstance::Target::AARCH64)
        , instance1("instance1", &manager, QemuInstance::Target::AARCH64)
        , counter("counter")
        , bridge0("bridge0", instance0, counter)
        , bridge1("bridge1", instance1, counter)
        , timer0("timer0", instance0, bridge0)
        , timer1("timer1", instance1, bridge1)
        , sse_clock("sse_clock", instance0)
        , sse_counter("sse_counter", instance0, sse_clock)
        , sse_timer("sse_timer", instance0, sse_counter)
    {
    }
};

qemu::MemoryRegion timer_region(qemu_arm_arch_timer_mmio& timer)
{
    qemu::SysBusDevice sysbus(timer.get_qemu_dev());
    return sysbus.mmio_get_region(0);
}

qemu::MemoryRegionOps::MemTxResult write_timer(
    QemuInstance& instance, qemu_arm_arch_timer_mmio& timer,
    uint64_t offset, uint64_t value, uint64_t size, bool secure = true)
{
    qemu::MemoryRegion region = timer_region(timer);
    qemu::MemoryRegionOps::MemTxResult result =
        qemu::MemoryRegionOps::MemTxError;
    instance.execute_on_iothread_sync([&] {
        qemu::MemoryRegionOps::MemTxAttrs attrs;
        attrs.secure = secure;
        attrs.user = false;
        result = region.dispatch_write(offset, value, size, attrs);
    });
    return result;
}

uint64_t read_timer(QemuInstance& instance,
                    qemu_arm_arch_timer_mmio& timer, uint64_t offset,
                    bool secure = true)
{
    qemu::MemoryRegion region = timer_region(timer);
    uint64_t value = 0;
    qemu::MemoryRegionOps::MemTxResult result =
        qemu::MemoryRegionOps::MemTxError;
    instance.execute_on_iothread_sync([&] {
        qemu::MemoryRegionOps::MemTxAttrs attrs;
        attrs.secure = secure;
        attrs.user = false;
        result = region.dispatch_read(offset, &value, 4, attrs);
    });
    EXPECT_EQ(result, qemu::MemoryRegionOps::MemTxOK);
    return value;
}

qemu::MemoryRegionOps::MemTxResult write_sse_timer(
    QemuInstance& instance, qemu_sse_timer& timer,
    uint64_t offset, uint64_t value)
{
    qemu::SysBusDevice sysbus(timer.get_qemu_dev());
    qemu::MemoryRegion region = sysbus.mmio_get_region(0);
    qemu::MemoryRegionOps::MemTxResult result =
        qemu::MemoryRegionOps::MemTxError;
    instance.execute_on_iothread_sync([&] {
        qemu::MemoryRegionOps::MemTxAttrs attrs;
        attrs.secure = true;
        attrs.user = false;
        result = region.dispatch_write(offset, value, 4, attrs);
    });
    return result;
}

qemu::MemoryRegionOps::MemTxResult write_sse_counter(
    QemuInstance& instance, qemu_sse_counter& counter,
    uint64_t offset, uint64_t value)
{
    qemu::SysBusDevice sysbus(counter.get_qemu_dev());
    qemu::MemoryRegion region = sysbus.mmio_get_region(0);
    qemu::MemoryRegionOps::MemTxResult result =
        qemu::MemoryRegionOps::MemTxError;
    instance.execute_on_iothread_sync([&] {
        qemu::MemoryRegionOps::MemTxAttrs attrs;
        attrs.secure = true;
        attrs.user = false;
        result = region.dispatch_write(offset, value, 4, attrs);
    });
    return result;
}

void program_frame0(QemuInstance& instance,
                    qemu_arm_arch_timer_mmio& timer)
{
    EXPECT_EQ(write_timer(instance, timer, 0x40, 0x3f, 4),
              qemu::MemoryRegionOps::MemTxOK);
    EXPECT_EQ(write_timer(instance, timer, 0x10020, 8, 8),
              qemu::MemoryRegionOps::MemTxOK);
    EXPECT_EQ(write_timer(instance, timer, 0x1002c, 1, 4),
              qemu::MemoryRegionOps::MemTxOK);
}

TEST(QemuArmGenericTimerBridge, TwoInstancesNotifySnapshotAndTeardown)
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);
    cci::cci_originator originator("timer-bridge-test");
    broker.set_preset_cci_value(
        "bench.timer0.cntfrq", cci::cci_value(uint64_t(125000000)),
        originator);
    broker.set_preset_cci_value(
        "bench.timer1.cntfrq", cci::cci_value(uint64_t(125000000)),
        originator);
    broker.set_preset_cci_value(
        "bench.sse_clock.frequency_hz", cci::cci_value(uint64_t(32000000)),
        originator);
    broker.set_preset_cci_value(
        "bench.instance0.qemu_args", cci::cci_value(std::string("-S")),
        originator);
    broker.set_preset_cci_value(
        "bench.instance1.qemu_args", cci::cci_value(std::string("-S")),
        originator);

    std::unique_ptr<TwoInstanceTimerBench> bench(
        new TwoInstanceTimerBench("bench"));
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    EXPECT_TRUE(bench->counter.snapshot().mutations_frozen);
    const qemu::ArmSSETimerSnapshot sse = bench->sse_timer.snapshot();
    EXPECT_EQ(sse.count, 0u);
    EXPECT_EQ(sse.counter_frequency_hz, 32000000u);
    EXPECT_EQ(sse.cntfrq, 0u);
    EXPECT_EQ(sse.ctl, 0u);
    EXPECT_EQ(sse.irq_level, 0u);
    EXPECT_EQ(write_sse_timer(bench->instance0, bench->sse_timer, 0x2c, 1),
              qemu::MemoryRegionOps::MemTxOK);
    EXPECT_EQ(bench->sse_timer.snapshot().ctl & 1u, 1u);
    bench->sse_timer.reset->write(true);
    EXPECT_EQ(bench->sse_timer.snapshot().ctl, 0u);
    bench->sse_timer.reset->write(false);
    EXPECT_EQ(write_sse_counter(bench->instance0, bench->sse_counter, 0x8,
                                123),
              qemu::MemoryRegionOps::MemTxOK);
    EXPECT_EQ(bench->sse_timer.snapshot().count, 123u);
    bench->sse_counter.reset->write(true);
    EXPECT_EQ(bench->sse_timer.snapshot().count, 0u);
    bench->sse_counter.reset->write(false);

    program_frame0(bench->instance0, bench->timer0);
    program_frame0(bench->instance1, bench->timer1);
    qemu::Object linked0 =
        bench->timer0.get_qemu_dev().get_prop_link("counter-provider");
    qemu::Object linked1 =
        bench->timer1.get_qemu_dev().get_prop_link("counter-provider");
    EXPECT_EQ(linked0.get_qemu_obj(),
              bench->bridge0.counter_provider().get_qemu_obj());
    EXPECT_EQ(linked1.get_qemu_obj(),
              bench->bridge1.counter_provider().get_qemu_obj());
    qemu::ArmArchTimerMMIOFrameSnapshot initial0 = bench->timer0.snapshot(0);
    qemu::ArmArchTimerMMIOFrameSnapshot initial1 = bench->timer1.snapshot(0);
    EXPECT_TRUE(initial0.count_accessible);
    EXPECT_TRUE(initial1.count_accessible);
    EXPECT_EQ(initial0.cntnsar_implemented, 1u);
    EXPECT_EQ(initial1.cntnsar_implemented, 1u);
    EXPECT_EQ(initial0.cval, 8u);
    EXPECT_EQ(initial1.cval, 8u);
    EXPECT_EQ(initial0.count, 0u);
    EXPECT_EQ(initial1.count, 0u);
    EXPECT_EQ(initial0.irq_level, 0u);
    EXPECT_EQ(initial1.irq_level, 0u);

    int64_t deadline = -1;
    ASSERT_TRUE(bench->counter.deadline_ns(8, 0, deadline));
    EXPECT_EQ(deadline, 64);
    bench->counter.reanchor_at(8, 0, 0);
    bench->bridge0.drain_notifications();
    bench->bridge1.drain_notifications();

    qemu::ArmArchTimerMMIOFrameSnapshot due0 = bench->timer0.snapshot(0);
    qemu::ArmArchTimerMMIOFrameSnapshot due1 = bench->timer1.snapshot(0);
    const auto epoch0 = bench->bridge0.epoch_snapshot();
    const auto epoch1 = bench->bridge1.epoch_snapshot();
    ASSERT_EQ(epoch0.systemc_ns, epoch1.systemc_ns);
    ASSERT_EQ(due0.qemu_virtual_ns + epoch0.offset_ns,
              epoch0.systemc_ns);
    ASSERT_EQ(due1.qemu_virtual_ns + epoch1.offset_ns,
              epoch1.systemc_ns);
    const auto common = bench->counter.snapshot_at(epoch0.systemc_ns);
    EXPECT_EQ(common.anchor_count, 8u);
    EXPECT_TRUE(due0.count_accessible);
    EXPECT_TRUE(due1.count_accessible);
    EXPECT_EQ(due0.cval, 8u);
    EXPECT_EQ(due1.cval, 8u);
    EXPECT_EQ(due0.count, 8u);
    EXPECT_EQ(due1.count, 8u);
    EXPECT_EQ(due0.irq_level, 1u);
    EXPECT_EQ(due1.irq_level, 1u);

    bench->counter.reanchor_at(0, 0, 0);
    bench->bridge0.drain_notifications();
    bench->bridge1.drain_notifications();
    EXPECT_EQ(bench->timer0.snapshot(0).irq_level, 0u);
    EXPECT_EQ(bench->timer1.snapshot(0).irq_level, 0u);

    EXPECT_EQ(epoch0.systemc_ns, epoch0.qemu_virtual_ns + epoch0.offset_ns);
    EXPECT_EQ(epoch1.systemc_ns, epoch1.qemu_virtual_ns + epoch1.offset_ns);
    EXPECT_EQ(due0.qemu_virtual_ns, epoch0.qemu_virtual_ns);
    EXPECT_EQ(due1.qemu_virtual_ns, epoch1.qemu_virtual_ns);
    EXPECT_EQ(epoch0.requested_generation, epoch0.delivered_generation);
    EXPECT_EQ(epoch1.requested_generation, epoch1.delivered_generation);

    EXPECT_EQ(write_timer(bench->instance0, bench->timer0, 0x44, 0x15, 4),
              qemu::MemoryRegionOps::MemTxOK);
    EXPECT_EQ(read_timer(bench->instance0, bench->timer0, 0x44, false), 0u);
    EXPECT_EQ(write_timer(bench->instance0, bench->timer0, 0x44, 0x3f, 4,
                          false),
              qemu::MemoryRegionOps::MemTxOK);
    EXPECT_EQ(read_timer(bench->instance0, bench->timer0, 0x44), 0x15u);
    auto secure = bench->timer0.snapshot_with_secure_frame_access(1);
    EXPECT_EQ(secure.cntacr, 0x3fu);
    EXPECT_TRUE(secure.count_accessible);
    EXPECT_TRUE(secure.frequency_accessible);
    EXPECT_TRUE(secure.timer_accessible);
    EXPECT_EQ(read_timer(bench->instance0, bench->timer0, 0x44), 0x15u);

    EXPECT_EQ(write_timer(bench->instance0, bench->timer0, 0x44, 0x9, 4),
              qemu::MemoryRegionOps::MemTxOK);
    EXPECT_THROW(bench->timer0.snapshot_with_secure_frame_access(1, 8),
                 qemu::LibQemuException);
    EXPECT_EQ(read_timer(bench->instance0, bench->timer0, 0x44), 0x9u);

    const uint64_t before_disconnect =
        bench->bridge0.epoch_snapshot().requested_generation;
    bench->bridge0.disconnect();
    bench->bridge1.disconnect();
    EXPECT_FALSE(bench->bridge0.active());
    EXPECT_FALSE(bench->bridge1.active());
    bench->counter.reanchor_at(1, 0, 0);
    EXPECT_EQ(bench->bridge0.epoch_snapshot().requested_generation,
              before_disconnect);

    sc_core::sc_stop();
}

}

int sc_main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
