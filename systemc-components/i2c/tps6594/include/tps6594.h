/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef QBOX_TPS6594_H
#define QBOX_TPS6594_H

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

class tps6594 : public sc_core::sc_module
{
public:
    static constexpr unsigned int NUM_GPIOS = 11;
    static constexpr unsigned int NUM_RAILS = 9;

    enum Register : uint16_t {
        DEV_REV = 0x001,
        NVM_CODE_1 = 0x002,
        NVM_CODE_2 = 0x003,
        BUCK1_CTRL = 0x004,
        BUCK1_VOUT_1 = 0x00e,
        LDO1_CTRL = 0x01d,
        LDO1_VOUT = 0x023,
        GPIO1_CONF = 0x031,
        GPIO_OUT_1 = 0x03d,
        GPIO_OUT_2 = 0x03e,
        GPIO_IN_1 = 0x03f,
        GPIO_IN_2 = 0x040,
        MASK_GPIO1_8_FALL = 0x04f,
        MASK_GPIO1_8_RISE = 0x050,
        MASK_GPIO9_11 = 0x051,
        INT_TOP = 0x05a,
        INT_BUCK = 0x05b,
        INT_BUCK1_2 = 0x05c,
        INT_BUCK3_4 = 0x05d,
        INT_BUCK5 = 0x05e,
        INT_LDO_VMON = 0x05f,
        INT_LDO1_2 = 0x060,
        INT_LDO3_4 = 0x061,
        INT_VMON = 0x062,
        INT_GPIO = 0x063,
        INT_GPIO1_8 = 0x064,
        INT_STARTUP = 0x065,
        INT_MISC = 0x066,
        INT_MODERATE_ERR = 0x067,
        INT_SEVERE_ERR = 0x068,
        INT_FSM_ERR = 0x069,
        INT_COMM_ERR = 0x06a,
        INT_READBACK_ERR = 0x06b,
        INT_ESM = 0x06c,
        FSM_I2C_TRIGGERS = 0x085,
        FSM_NSLEEP_TRIGGERS = 0x086,
        RTC_SECONDS = 0x0b5,
        RTC_MINUTES = 0x0b6,
        RTC_HOURS = 0x0b7,
        RTC_DAYS = 0x0b8,
        RTC_MONTHS = 0x0b9,
        RTC_YEARS = 0x0ba,
        RTC_WEEKS = 0x0bb,
        ALARM_SECONDS = 0x0bc,
        ALARM_MINUTES = 0x0bd,
        ALARM_HOURS = 0x0be,
        ALARM_DAYS = 0x0bf,
        ALARM_MONTHS = 0x0c0,
        ALARM_YEARS = 0x0c1,
        RTC_CTRL_1 = 0x0c2,
        RTC_CTRL_2 = 0x0c3,
        RTC_STATUS = 0x0c4,
        RTC_INTERRUPTS = 0x0c5,
        SERIAL_IF_CONFIG = 0x11a,
    };

    cci::cci_param<uint32_t> p_address;
    cci::cci_param<sc_core::sc_time> p_access_latency;
    cci::cci_param<uint32_t> p_dev_rev;
    cci::cci_param<uint32_t> p_nvm_code_1;
    cci::cci_param<uint32_t> p_nvm_code_2;
    cci::cci_param<uint32_t> p_gpio_pullups;
    cci::cci_param<uint32_t> p_gpio_irq_mask;

    tlm_utils::simple_target_socket<tps6594, DEFAULT_TLM_BUSWIDTH> i2c_socket;
    InitiatorSignalSocket<bool> int_n;
    sc_core::sc_vector<TargetSignalSocket<bool>> gpio_in;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> gpio_out;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> gpio_oe;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> rail_enabled;
    sc_core::sc_vector<InitiatorSignalSocket<uint32_t>> rail_voltage_uv;

    SC_HAS_PROCESS(tps6594);
    explicit tps6594(sc_core::sc_module_name name);

    void before_end_of_elaboration() override;

private:
    static constexpr unsigned int NUM_PAGES = 5;
    using Page = std::array<uint8_t, 256>;

    std::array<Page, NUM_PAGES> m_registers{};
    std::array<bool, NUM_GPIOS> m_gpio_inputs{};
    std::array<uint8_t, 7> m_rtc_shadow{};
    uint8_t m_pointer = 0;
    uint8_t m_page = 0;
    bool m_transaction_active = false;
    bool m_expect_register = true;
    bool m_rtc_running = false;
    bool m_int_n_level = true;
    uint64_t m_rtc_base_seconds = 0;
    sc_core::sc_time m_rtc_base_time = sc_core::SC_ZERO_TIME;

    gs::async_event m_output_event;
    gs::async_event m_rail_event;
    gs::async_event m_irq_event;
    sc_core::sc_event m_alarm_event;
    sc_core::sc_signal<bool> m_irq_stub;
    sc_core::sc_vector<sc_core::sc_signal<bool>> m_gpio_output_stubs;
    sc_core::sc_vector<sc_core::sc_signal<bool>> m_gpio_oe_stubs;
    sc_core::sc_vector<sc_core::sc_signal<bool>> m_rail_enabled_stubs;
    sc_core::sc_vector<sc_core::sc_signal<uint32_t>> m_rail_voltage_stubs;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);
    void reset_device();
    uint8_t read_register(unsigned int page, uint8_t reg);
    void write_register(unsigned int page, uint8_t reg, uint8_t value);
    void set_gpio_input(unsigned int pin, bool level);
    uint8_t gpio_input_register(unsigned int bank) const;
    bool gpio_output_level(unsigned int pin) const;
    void drive_gpio_outputs();
    void drive_rails();
    void drive_interrupt();
    void update_interrupt();
    bool interrupt_pending() const;
    uint8_t interrupt_summary(uint8_t reg) const;
    void update_rtc_running();
    uint64_t rtc_seconds_now() const;
    void load_rtc_base();
    void store_rtc(uint64_t seconds, std::array<uint8_t, 7>& target) const;
    uint64_t decode_rtc(const uint8_t* regs) const;
    void capture_rtc_shadow();
    void schedule_alarm();
    void alarm_fired();
    static uint32_t rail_voltage(unsigned int rail, uint8_t selector);
};

extern "C" void module_register();

#endif
