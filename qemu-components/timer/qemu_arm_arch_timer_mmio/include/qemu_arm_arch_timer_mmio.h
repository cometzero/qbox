#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <cci_configuration>

#include <device.h>
#include <module_factory_registery.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/target.h>
#include <qemu_arm_generic_timer_counter_bridge.h>

class qemu_arm_arch_timer_mmio : public QemuDevice
{
    static constexpr unsigned int max_frames = 8;
    static constexpr uint64_t default_view_size = 0x10000;
    static constexpr uint64_t default_cntfrq = 62500000;

    cci::cci_param<uint64_t> p_cntfrq;
    cci::cci_param<uint64_t> p_frequency;
    cci::cci_param<unsigned int> p_nr_frames;
    cci::cci_param<uint64_t> p_view_size;
    std::vector<std::unique_ptr<cci::cci_param<uint64_t> > > p_frame_offset;
    std::vector<std::unique_ptr<cci::cci_param<unsigned int> > > p_frame_id;
    std::vector<std::unique_ptr<cci::cci_param<unsigned int> > > p_cntacr_reset;
    cci::cci_param<unsigned int> p_cntnsar_reset;
    qemu::MemoryRegion m_control_region;

public:
    QemuTargetSocket<> socket;
    sc_core::sc_vector<QemuInitiatorSignalSocket> irq;

    qemu_arm_arch_timer_mmio(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qemu_arm_arch_timer_mmio(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qemu_arm_arch_timer_mmio(sc_core::sc_module_name nm, QemuInstance& inst)
        : QemuDevice(nm, inst, "arm_arch_timer_mmio")
        , p_cntfrq("cntfrq", default_cntfrq, "Counter frequency in Hz")
        , p_frequency("frequency", 0, "Alias for cntfrq when nonzero")
        , p_nr_frames("nr_frames", 2, "Number of MMIO timer frames")
        , p_view_size("view_size", default_view_size, "MMIO size of each timer frame")
        , p_cntnsar_reset("cntnsar_reset", 0, "CNTNSAR reset value")
        , socket("mem", inst)
        , irq("irq")
    {
        init_frame_params();
        validate_params();
        irq.init(p_nr_frames.get_value(), [](const char* n, size_t i) { return new QemuInitiatorSignalSocket(n); });
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        validate_params();

        m_dev.set_prop_uint("cntfrq", timer_frequency());
        m_dev.set_prop_uint("nr-frames", p_nr_frames.get_value());
        m_dev.set_prop_uint("view-size", p_view_size.get_value());

        for (unsigned int i = 0; i < max_frames; ++i) {
            m_dev.set_prop_uint(frame_property("frame-offset-", i).c_str(), p_frame_offset[i]->get_value());
            m_dev.set_prop_uint(frame_property("frame-id-", i).c_str(), p_frame_id[i]->get_value());
            m_dev.set_prop_uint(frame_property("cntacr-reset-", i).c_str(), p_cntacr_reset[i]->get_value());
        }
        m_dev.set_prop_uint("cntnsar-reset", p_cntnsar_reset.get_value());
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        m_control_region = sbd.mmio_get_region(0);
        socket.init(sbd, 0);

        for (unsigned int i = 0; i < p_nr_frames.get_value(); ++i) {
            irq[i].init_sbd(sbd, i);
        }
    }

    qemu::ArmArchTimerMMIOFrameSnapshot snapshot(uint32_t frame);
    qemu::ArmArchTimerMMIOFrameSnapshot
    snapshot_with_secure_frame_access(uint32_t frame);
    qemu::ArmArchTimerMMIOFrameSnapshot
    snapshot_with_secure_frame_access(uint32_t access_frame,
                                      uint32_t snapshot_frame);

private:
    static std::string frame_property(const char* prefix, unsigned int index)
    {
        return std::string(prefix) + std::to_string(index);
    }

    void init_frame_params()
    {
        p_frame_offset.reserve(max_frames);
        p_frame_id.reserve(max_frames);
        p_cntacr_reset.reserve(max_frames);

        for (unsigned int i = 0; i < max_frames; ++i) {
            const uint64_t default_offset = default_view_size * (i + 1);
            p_frame_offset.emplace_back(new cci::cci_param<uint64_t>(
                frame_property("frame_offset_", i).c_str(), default_offset, "Frame MMIO offset"));
            p_frame_id.emplace_back(new cci::cci_param<unsigned int>(
                frame_property("frame_id_", i).c_str(), i, "Frame ID reported through CNTFID"));
            p_cntacr_reset.emplace_back(new cci::cci_param<unsigned int>(
                frame_property("cntacr_reset_", i).c_str(), 0, "CNTACR reset value"));
        }
    }

    uint64_t timer_frequency()
    {
        const uint64_t cntfrq = p_cntfrq.get_value();
        const uint64_t frequency = p_frequency.get_value();

        if (!p_frequency.is_default_value()) {
            if (!p_cntfrq.is_default_value() && cntfrq != frequency) {
                SCP_FATAL(SCMOD) << "cntfrq and frequency specify different values";
            }
            return frequency;
        }

        return cntfrq;
    }

    void validate_params()
    {
        const uint64_t freq = timer_frequency();

        if (freq == 0 || freq > std::numeric_limits<uint32_t>::max()) {
            SCP_FATAL(SCMOD) << "cntfrq/frequency must be a nonzero 32-bit value";
        }

        if (p_nr_frames.get_value() == 0 || p_nr_frames.get_value() > max_frames) {
            SCP_FATAL(SCMOD) << "nr_frames must be in the range 1.." << max_frames;
        }

        if (p_view_size.get_value() < 0x1000) {
            SCP_FATAL(SCMOD) << "view_size must be at least 0x1000";
        }
    }
};

class qemu_arm_arch_timer_mmio_external_counter
    : public qemu_arm_arch_timer_mmio
{
private:
    qemu_arm_generic_timer_counter_bridge& m_bridge;

public:
    qemu_arm_arch_timer_mmio_external_counter(
        const sc_core::sc_module_name& name, sc_core::sc_object* instance,
        sc_core::sc_object* bridge);
    qemu_arm_arch_timer_mmio_external_counter(
        const sc_core::sc_module_name& name, QemuInstance& instance,
        qemu_arm_generic_timer_counter_bridge& bridge);
    void before_end_of_elaboration() override;
};

extern "C" void module_register();
