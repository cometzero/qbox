/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <cstdint>

#include <cci_configuration>
#include <systemc>

#include <libqemu-cxx/libqemu-cxx.h>
#include <module_factory_registery.h>
#include <qemu-instance.h>

class qemu_clock_source : public sc_core::sc_module
{
private:
    QemuInstance& m_inst;
    cci::cci_param<uint64_t> p_frequency_hz;
    qemu::Object m_owner;
    qemu::Clock m_clock;
    bool m_initialized = false;

    void initialize();

public:
    qemu_clock_source(const sc_core::sc_module_name& name,
                      sc_core::sc_object* instance);
    qemu_clock_source(const sc_core::sc_module_name& name,
                      QemuInstance& instance);

    const qemu::Clock& clock();
    uint64_t frequency_hz() const { return p_frequency_hz.get_value(); }
    void before_end_of_elaboration() override;
};

extern "C" void module_register();
