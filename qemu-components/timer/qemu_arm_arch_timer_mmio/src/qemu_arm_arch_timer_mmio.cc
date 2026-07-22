#include <systemc>

#include <qemu_arm_arch_timer_mmio.h>

#include <exception>
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

qemu::ArmArchTimerMMIOFrameSnapshot qemu_arm_arch_timer_mmio::snapshot(
    uint32_t frame)
{
    return m_inst.get().arm_arch_timer_mmio_frame_snapshot(m_dev, frame);
}

qemu::ArmArchTimerMMIOFrameSnapshot
qemu_arm_arch_timer_mmio::snapshot_with_secure_frame_access(uint32_t frame)
{
    return snapshot_with_secure_frame_access(frame, frame);
}

qemu::ArmArchTimerMMIOFrameSnapshot
qemu_arm_arch_timer_mmio::snapshot_with_secure_frame_access(
    uint32_t access_frame, uint32_t snapshot_frame)
{
    if (!m_control_region.valid()) {
        throw std::logic_error("MMIO timer control region is not initialized");
    }
    qemu::ArmArchTimerMMIOFrameSnapshot result;
    m_inst.execute_on_iothread_sync([&] {
        qemu::MemoryRegion::MemTxAttrs attrs;
        attrs.secure = true;
        attrs.user = false;
        const uint64_t cntacr_offset = 0x40 + uint64_t(access_frame) * 4;
        uint64_t original = 0;
        bool restore = false;
        std::exception_ptr operation_error;
        try {
            if (m_control_region.dispatch_read(cntacr_offset, &original, 4,
                                               attrs) !=
                qemu::MemoryRegionOps::MemTxOK) {
                throw qemu::LibQemuException("CNTACR read failed");
            }
            if (m_control_region.dispatch_write(cntacr_offset, 0x3f, 4,
                                                attrs) !=
                qemu::MemoryRegionOps::MemTxOK) {
                throw qemu::LibQemuException("CNTACR temporary write failed");
            }
            restore = true;
            result = m_inst.get().arm_arch_timer_mmio_frame_snapshot_on_iothread(
                m_dev, snapshot_frame);
        } catch (...) {
            operation_error = std::current_exception();
        }
        if (restore &&
            m_control_region.dispatch_write(cntacr_offset, original, 4,
                                            attrs) !=
                qemu::MemoryRegionOps::MemTxOK) {
            throw qemu::LibQemuException("CNTACR restoration failed");
        }
        if (operation_error) {
            std::rethrow_exception(operation_error);
        }
    });
    return result;
}

qemu_arm_arch_timer_mmio_external_counter::
    qemu_arm_arch_timer_mmio_external_counter(
        const sc_core::sc_module_name& name, sc_core::sc_object* instance,
        sc_core::sc_object* bridge)
    : qemu_arm_arch_timer_mmio_external_counter(
          name, require_object<QemuInstance>(instance, "QemuInstance"),
          require_object<qemu_arm_generic_timer_counter_bridge>(
              bridge, "qemu_arm_generic_timer_counter_bridge"))
{
}

qemu_arm_arch_timer_mmio_external_counter::
    qemu_arm_arch_timer_mmio_external_counter(
        const sc_core::sc_module_name& name, QemuInstance& instance,
        qemu_arm_generic_timer_counter_bridge& bridge)
    : qemu_arm_arch_timer_mmio(name, instance), m_bridge(bridge)
{
}

void qemu_arm_arch_timer_mmio_external_counter::before_end_of_elaboration()
{
    qemu_arm_arch_timer_mmio::before_end_of_elaboration();
    m_dev.set_prop_bool("access-control", true);
    m_dev.set_prop_link("counter-provider", m_bridge.counter_provider());
}

void module_register()
{
    GSC_MODULE_REGISTER_C(qemu_arm_arch_timer_mmio, sc_core::sc_object*);
    GSC_MODULE_REGISTER_C(qemu_arm_arch_timer_mmio_external_counter,
                          sc_core::sc_object*, sc_core::sc_object*);
}
