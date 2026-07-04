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
        }
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        socket.init(sbd, 0);

        for (unsigned int i = 0; i < p_nr_frames.get_value(); ++i) {
            irq[i].init_sbd(sbd, i);
        }
    }

private:
    static std::string frame_property(const char* prefix, unsigned int index)
    {
        return std::string(prefix) + std::to_string(index);
    }

    void init_frame_params()
    {
        p_frame_offset.reserve(max_frames);
        p_frame_id.reserve(max_frames);

        for (unsigned int i = 0; i < max_frames; ++i) {
            const uint64_t default_offset = default_view_size * (i + 1);
            p_frame_offset.emplace_back(new cci::cci_param<uint64_t>(
                frame_property("frame_offset_", i).c_str(), default_offset, "Frame MMIO offset"));
            p_frame_id.emplace_back(new cci::cci_param<unsigned int>(
                frame_property("frame_id_", i).c_str(), i, "Frame ID reported through CNTFID"));
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

extern "C" void module_register();
