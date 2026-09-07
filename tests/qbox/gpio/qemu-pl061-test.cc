/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <ports/target-signal-socket.h>
#include <qemu-instance.h>
#include <qemu_pl061.h>

#include "test/test.h"

class QemuPl061Test : public TestBench
{
    static constexpr uint64_t data_all = 0x3fc;
    static constexpr uint64_t direction = 0x400;
    static constexpr uint64_t interrupt_sense = 0x404;
    static constexpr uint64_t interrupt_both_edges = 0x408;
    static constexpr uint64_t interrupt_event = 0x40c;
    static constexpr uint64_t interrupt_mask = 0x410;
    static constexpr uint64_t interrupt_clear = 0x41c;

    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst;
    qemu_pl061 m_gpio;
    tlm_utils::simple_initiator_socket<QemuPl061Test> m_mmio;
    TargetSignalSocket<bool> m_irq;
    sc_core::sc_vector<TargetSignalSocket<bool> > m_gpio_out;

    uint32_t read_reg(uint64_t address)
    {
        uint32_t value = 0;
        tlm::tlm_generic_payload payload;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        payload.set_command(tlm::TLM_READ_COMMAND);
        payload.set_address(address);
        payload.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        payload.set_data_length(sizeof(value));
        payload.set_streaming_width(sizeof(value));
        payload.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        m_mmio->b_transport(payload, delay);
        TEST_ASSERT(payload.get_response_status() == tlm::TLM_OK_RESPONSE);
        return value;
    }

    void write_reg(uint64_t address, uint32_t value)
    {
        tlm::tlm_generic_payload payload;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;

        payload.set_command(tlm::TLM_WRITE_COMMAND);
        payload.set_address(address);
        payload.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        payload.set_data_length(sizeof(value));
        payload.set_streaming_width(sizeof(value));
        payload.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        m_mmio->b_transport(payload, delay);
        TEST_ASSERT(payload.get_response_status() == tlm::TLM_OK_RESPONSE);
    }

    void run_test()
    {
        wait(sc_core::SC_ZERO_TIME);

        TEST_ASSERT(read_reg(0xfe0) == 0x61);
        TEST_ASSERT(read_reg(0xff0) == 0x0d);
        TEST_ASSERT(read_reg(data_all) == 0x5a);

        qemu_pl061::RuntimePinSnapshot snapshot;
        TEST_ASSERT(m_gpio.runtime_pin_snapshot(0, snapshot));
        TEST_ASSERT(!snapshot.direction_output);
        TEST_ASSERT(!snapshot.data_level);
        TEST_ASSERT(!snapshot.input_level);
        TEST_ASSERT(!snapshot.initial_input_level);
        TEST_ASSERT(!m_gpio.runtime_pin_snapshot(8, snapshot));

        TEST_ASSERT(m_gpio.runtime_drive_input(0, true));
        TEST_ASSERT(read_reg(data_all) == 0x5b);
        TEST_ASSERT(m_gpio.runtime_release_input(0));
        TEST_ASSERT(read_reg(data_all) == 0x5a);

        TEST_ASSERT(m_gpio.runtime_set_direction(1, true));
        TEST_ASSERT((read_reg(direction) & 0x02) != 0);
        TEST_ASSERT(m_gpio.runtime_write_output(1, true));
        wait(sc_core::SC_ZERO_TIME);
        TEST_ASSERT(m_gpio_out[1].read());
        TEST_ASSERT(m_gpio.runtime_pin_snapshot(1, snapshot));
        TEST_ASSERT(snapshot.direction_output);
        TEST_ASSERT(snapshot.data_level);
        TEST_ASSERT(!m_gpio.runtime_drive_input(1, false));

        TEST_ASSERT(m_gpio.runtime_write_output(1, false));
        wait(sc_core::SC_ZERO_TIME);
        TEST_ASSERT(!m_gpio_out[1].read());
        TEST_ASSERT(!m_gpio.runtime_write_output(0, true));

        TEST_ASSERT(m_gpio.runtime_drive_input(0, false));
        write_reg(interrupt_sense, 0x00);
        write_reg(interrupt_both_edges, 0x00);
        write_reg(interrupt_event, 0x01);
        write_reg(interrupt_mask, 0x01);
        TEST_ASSERT(m_gpio.runtime_drive_input(0, true));
        wait(sc_core::SC_ZERO_TIME);
        TEST_ASSERT(m_irq.read());

        write_reg(interrupt_clear, 0x01);
        wait(sc_core::SC_ZERO_TIME);
        TEST_ASSERT(!m_irq.read());

        m_gpio.gpio_in[0]->write(true);
        m_inst.reset->write(true);
        m_gpio.reset->write(true);
        wait(sc_core::sc_time(1, sc_core::SC_US));
        m_inst.reset->write(false);
        m_gpio.reset->write(false);
        wait(sc_core::sc_time(1, sc_core::SC_US));
        TEST_ASSERT((read_reg(data_all) & 0x01) != 0);
        TEST_ASSERT(m_gpio_out[1].read());
        TEST_ASSERT(m_gpio.runtime_release_input(0));
        TEST_ASSERT((read_reg(data_all) & 0x01) == 0);

        sc_core::sc_stop();
    }

public:
    QemuPl061Test(const sc_core::sc_module_name& name)
        : TestBench(name)
        , m_inst("inst", &m_inst_manager, qemu::Target::AARCH64)
        , m_gpio("gpio", m_inst)
        , m_mmio("mmio")
        , m_irq("irq")
        , m_gpio_out("gpio_out", 8, [](const char* n, std::size_t) { return new TargetSignalSocket<bool>(n); })
    {
        m_mmio.bind(m_gpio.mem);
        m_gpio.irq.bind(m_irq);
        for (std::size_t i = 0; i < m_gpio_out.size(); ++i) {
            m_gpio.gpio_out[i].bind(m_gpio_out[i]);
        }

        SC_THREAD(run_test);
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<QemuPl061Test>(argc, argv); }
