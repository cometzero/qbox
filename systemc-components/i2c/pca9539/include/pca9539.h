/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef QBOX_PCA9539_H
#define QBOX_PCA9539_H

#include <array>
#include <cstdint>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

#include <async_event.h>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <tlm_sockets_buswidth.h>

class pca9539 : public sc_core::sc_module
{
public:
    static constexpr unsigned int NUM_GPIOS = 16;

    enum Register : uint8_t {
        INPUT_PORT_0 = 0x00,
        INPUT_PORT_1 = 0x01,
        OUTPUT_PORT_0 = 0x02,
        OUTPUT_PORT_1 = 0x03,
        POLARITY_PORT_0 = 0x04,
        POLARITY_PORT_1 = 0x05,
        CONFIG_PORT_0 = 0x06,
        CONFIG_PORT_1 = 0x07,
    };

    cci::cci_param<uint32_t> p_address;
    cci::cci_param<sc_core::sc_time> p_access_latency;

    tlm_utils::simple_target_socket<pca9539, DEFAULT_TLM_BUSWIDTH> i2c_socket;
    // Physical active-low reset level.
    TargetSignalSocket<bool> reset_n;
    // Physical open-drain level: false is asserted, true is released.
    InitiatorSignalSocket<bool> int_n;
    sc_core::sc_vector<TargetSignalSocket<bool>> gpio_in;
    // Effective pin level: output latch when driven, sampled input otherwise.
    sc_core::sc_vector<InitiatorSignalSocket<bool>> gpio_out;
    // True when the corresponding push-pull output driver is enabled.
    sc_core::sc_vector<InitiatorSignalSocket<bool>> gpio_oe;

    SC_HAS_PROCESS(pca9539);
    explicit pca9539(sc_core::sc_module_name name);

    void before_end_of_elaboration() override;

private:
    std::array<uint8_t, 2> m_output{ { 0xff, 0xff } };
    std::array<uint8_t, 2> m_polarity{ { 0x00, 0x00 } };
    std::array<uint8_t, 2> m_config{ { 0xff, 0xff } };
    std::array<uint8_t, 2> m_input_latch{ { 0x00, 0x00 } };
    std::array<bool, NUM_GPIOS> m_input_levels{};
    uint8_t m_pointer = INPUT_PORT_0;
    bool m_transaction_active = false;
    bool m_expect_command = true;
    bool m_reset_asserted = true;
    bool m_int_n_level = true;

    gs::async_event m_output_event;
    gs::async_event m_irq_event;
    sc_core::sc_signal<bool> m_irq_stub;
    sc_core::sc_vector<sc_core::sc_signal<bool>> m_output_stubs;
    sc_core::sc_vector<sc_core::sc_signal<bool>> m_oe_stubs;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    void reset_device();
    void set_reset_n(bool level);
    void set_gpio_input(unsigned int pin, bool level);
    uint8_t physical_port(unsigned int bank) const;
    uint8_t read_register(uint8_t reg);
    void write_register(uint8_t reg, uint8_t value);
    void advance_pointer();
    void update_interrupt();
    void drive_interrupt();
    void drive_outputs();
};

extern "C" void module_register();

#endif
