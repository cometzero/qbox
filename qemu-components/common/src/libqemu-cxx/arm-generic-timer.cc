/* SPDX-License-Identifier: BSD-3-Clause */

#include <libqemu/libqemu.h>

#include <libqemu-cxx/libqemu-cxx.h>
#include <internals.h>

#include <cstddef>
#include <utility>

namespace qemu {

Clock LibQemu::clock_new(const Object& parent, const char* name)
{
    const size_t end = offsetof(LibQemuExports, clock_new) +
        sizeof(((LibQemuExports*)nullptr)->clock_new);
    m_int->require_v2_export(end, "clock_new");
    QemuClock* clock = m_int->exports().clock_new(parent.get_qemu_obj(), name);
    if (clock == nullptr) throw LibQemuException("failed to create QEMU clock");
    return Clock(clock, m_int);
}

std::shared_ptr<IOThreadJob> LibQemu::iothread_job_new(
    std::function<void()> callback)
{
    return std::make_shared<IOThreadJob>(m_int, std::move(callback));
}

void LibQemu::execute_on_iothread_sync(std::function<void()> callback)
{
    std::shared_ptr<IOThreadJob> job = iothread_job_new(std::move(callback));
    if (!job->schedule()) {
        throw LibQemuException("failed to schedule libqemu iothread job");
    }
    job->drain();
    job->rethrow_if_failed();
}

ArmGenericTimerCounterProxy LibQemu::arm_generic_timer_counter_proxy_new(
    const LibQemuArmGenericTimerCounterCallbacks& callbacks, void* opaque)
{
    const size_t end = offsetof(LibQemuExports, arm_generic_timer_counter_notify) +
        sizeof(((LibQemuExports*)nullptr)->arm_generic_timer_counter_notify);
    m_int->require_v2_export(end, "ARM generic timer counter proxy API");
    QemuObject* raw = m_int->exports().arm_generic_timer_counter_proxy_new(
        &callbacks, opaque);
    if (raw == nullptr) throw LibQemuException("failed to create ARM timer proxy");
    m_int->exports().object_property_add_child(
        m_int->exports().object_get_root(),
        "qbox-arm-timer-counter[*]", raw);
    Object object(raw, m_int);
    m_int->exports().object_unref(raw);
    return ArmGenericTimerCounterProxy(object);
}

ArmCpuGenericTimerSnapshot LibQemu::arm_cpu_generic_timer_snapshot(
    const Device& cpu, ArmGenericTimerOutput output)
{
    if (cpu.get_inst_id() != reinterpret_cast<uintptr_t>(m_int.get()))
        throw LibQemuException("CPU snapshot belongs to another QEMU instance");
    const size_t end = offsetof(LibQemuExports, cpu_arm_generic_timer_snapshot) +
        sizeof(((LibQemuExports*)nullptr)->cpu_arm_generic_timer_snapshot);
    m_int->require_v2_export(end, "cpu_arm_generic_timer_snapshot");
    LibQemuArmCpuGenericTimerSnapshot raw = {};
    raw.size = sizeof(raw);
    raw.version = LIBQEMU_ARM_TIMER_SNAPSHOT_ABI;
    bool success = false;
    execute_on_iothread_sync([&] {
        success = m_int->exports().cpu_arm_generic_timer_snapshot(
            cpu.get_qemu_obj(), static_cast<LibQemuArmGenericTimerOutput>(output),
            &raw);
    });
    if (!success) throw LibQemuException("ARM CPU timer snapshot failed");
    ArmCpuGenericTimerSnapshot result;
    result.qemu_virtual_ns = raw.qemu_virtual_ns;
    result.physical_count = raw.physical_count;
    result.cval = raw.cval;
    result.cntfrq = raw.cntfrq;
    result.ctl = raw.ctl;
    result.irq_level = raw.irq_level;
    return result;
}

ArmArchTimerMMIOFrameSnapshot LibQemu::arm_arch_timer_mmio_frame_snapshot(
    const Device& timer, uint32_t frame)
{
    ArmArchTimerMMIOFrameSnapshot result;
    execute_on_iothread_sync([&] {
        result = arm_arch_timer_mmio_frame_snapshot_on_iothread(timer, frame);
    });
    return result;
}

ArmArchTimerMMIOFrameSnapshot
LibQemu::arm_arch_timer_mmio_frame_snapshot_on_iothread(
    const Device& timer, uint32_t frame)
{
    if (timer.get_inst_id() != reinterpret_cast<uintptr_t>(m_int.get()))
        throw LibQemuException("MMIO snapshot belongs to another QEMU instance");
    const size_t end = offsetof(LibQemuExports, arm_arch_timer_mmio_frame_snapshot) +
        sizeof(((LibQemuExports*)nullptr)->arm_arch_timer_mmio_frame_snapshot);
    m_int->require_v2_export(end, "arm_arch_timer_mmio_frame_snapshot");
    LibQemuArmArchTimerMMIOFrameSnapshot raw = {};
    raw.size = sizeof(raw);
    raw.version = LIBQEMU_ARM_TIMER_SNAPSHOT_ABI;
    if (!m_int->exports().arm_arch_timer_mmio_frame_snapshot(
            timer.get_qemu_obj(), frame, &raw))
        throw LibQemuException("ARM MMIO timer snapshot failed");
    ArmArchTimerMMIOFrameSnapshot result;
    result.qemu_virtual_ns = raw.qemu_virtual_ns;
    result.count = raw.count;
    result.cval = raw.cval;
    result.cntfrq = raw.cntfrq;
    result.cntacr = raw.cntacr;
    result.cntpl0acr = raw.cntpl0acr;
    result.cntnsar = raw.cntnsar;
    result.cntnsar_implemented = raw.cntnsar_implemented != 0;
    result.ctl = raw.ctl;
    result.irq_level = raw.irq_level;
    result.count_accessible = raw.count_accessible != 0;
    result.frequency_accessible = raw.frequency_accessible != 0;
    result.timer_accessible = raw.timer_accessible != 0;
    return result;
}

ArmSSETimerSnapshot LibQemu::arm_sse_timer_snapshot(
    const Device& counter, const Device& timer)
{
    const uintptr_t instance = reinterpret_cast<uintptr_t>(m_int.get());
    if (counter.get_inst_id() != instance || timer.get_inst_id() != instance)
        throw LibQemuException("SSE snapshot belongs to another QEMU instance");
    const size_t end = offsetof(LibQemuExports, arm_sse_timer_snapshot) +
        sizeof(((LibQemuExports*)nullptr)->arm_sse_timer_snapshot);
    m_int->require_v2_export(end, "arm_sse_timer_snapshot");
    LibQemuArmSSETimerSnapshot raw = {};
    raw.size = sizeof(raw);
    raw.version = LIBQEMU_ARM_TIMER_SNAPSHOT_ABI;
    bool success = false;
    execute_on_iothread_sync([&] {
        success = m_int->exports().arm_sse_timer_snapshot(
            counter.get_qemu_obj(), timer.get_qemu_obj(), &raw);
    });
    if (!success) throw LibQemuException("ARM SSE timer snapshot failed");
    ArmSSETimerSnapshot result;
    result.qemu_virtual_ns = raw.qemu_virtual_ns;
    result.count = raw.count;
    result.cval = raw.cval;
    result.counter_frequency_hz = raw.counter_frequency_hz;
    result.cntfrq = raw.cntfrq;
    result.ctl = raw.ctl;
    result.irq_level = raw.irq_level;
    return result;
}

}
