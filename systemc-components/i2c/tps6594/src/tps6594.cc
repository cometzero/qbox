/* SPDX-License-Identifier: BSD-3-Clause */

#include "tps6594.h"

#include <algorithm>

#include <i2c-transaction.h>

namespace {
constexpr uint8_t GPIO_DIR = 1U << 0;
constexpr uint8_t GPIO_OD = 1U << 1;
constexpr uint8_t GPIO_SEL_MASK = 0xe0;
constexpr uint8_t RAIL_ENABLE = 1U << 0;
constexpr uint8_t RTC_STOP = 1U << 0;
constexpr uint8_t RTC_RUN = 1U << 1;
constexpr uint8_t RTC_GET_TIME = 1U << 6;
constexpr uint8_t RTC_POWER_UP = 1U << 7;
constexpr uint8_t RTC_ALARM = 1U << 6;
constexpr uint8_t RTC_TIMER = 1U << 5;
constexpr uint8_t RTC_XTAL_ENABLE = 1U << 0;
constexpr uint8_t RTC_IT_ALARM = 1U << 3;
constexpr uint8_t CRC_ENABLE = 1U << 1;

uint8_t bcd_to_binary(uint8_t value) { return (value >> 4) * 10 + (value & 0x0f); }
uint8_t binary_to_bcd(unsigned int value) { return static_cast<uint8_t>(((value / 10) << 4) | (value % 10)); }

bool leap_year(unsigned int year) { return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0); }

unsigned int month_days(unsigned int year, unsigned int month)
{
    static const unsigned int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return days[month - 1] + (month == 2 && leap_year(year));
}
} // namespace

tps6594::tps6594(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , p_address("address", 0x48, "Page 0 7-bit I2C address")
    , p_access_latency("access_latency", sc_core::sc_time(100, sc_core::SC_NS), "I2C byte latency")
    , p_dev_rev("dev_rev", 0x00, "QBox NVM profile device revision")
    , p_nvm_code_1("nvm_code_1", 0x00, "QBox NVM profile code")
    , p_nvm_code_2("nvm_code_2", 0x00, "QBox NVM profile revision")
    , p_gpio_pullups("gpio_pullups", 0x000, "External pull-up bitmap for GPIO outputs")
    , p_gpio_irq_mask("gpio_irq_mask", 0x000, "NVM GPIO edge interrupt mask bitmap")
    , i2c_socket("i2c_socket")
    , int_n("int_n")
    , gpio_in("gpio_in", NUM_GPIOS)
    , gpio_out("gpio_out", NUM_GPIOS)
    , gpio_oe("gpio_oe", NUM_GPIOS)
    , rail_enabled("rail_enabled", NUM_RAILS)
    , rail_voltage_uv("rail_voltage_uv", NUM_RAILS)
    , m_output_event(false)
    , m_rail_event(false)
    , m_irq_event(false)
    , m_irq_stub("irq_stub")
    , m_gpio_output_stubs("gpio_output_stub", NUM_GPIOS)
    , m_gpio_oe_stubs("gpio_oe_stub", NUM_GPIOS)
    , m_rail_enabled_stubs("rail_enabled_stub", NUM_RAILS)
    , m_rail_voltage_stubs("rail_voltage_stub", NUM_RAILS)
{
    i2c_socket.register_b_transport(this, &tps6594::b_transport);
    for (unsigned int pin = 0; pin < NUM_GPIOS; ++pin) {
        gpio_in[pin].register_value_changed_cb([this, pin](bool level) { set_gpio_input(pin, level); });
    }

    reset_device();

    SC_METHOD(drive_gpio_outputs);
    sensitive << m_output_event;
    SC_METHOD(drive_rails);
    sensitive << m_rail_event;
    SC_METHOD(drive_interrupt);
    sensitive << m_irq_event;
    SC_METHOD(alarm_fired);
    sensitive << m_alarm_event;
    dont_initialize();
}

void tps6594::before_end_of_elaboration()
{
    if (!int_n.get_interface()) int_n.bind(m_irq_stub);
    for (unsigned int pin = 0; pin < NUM_GPIOS; ++pin) {
        if (!gpio_out[pin].get_interface()) gpio_out[pin].bind(m_gpio_output_stubs[pin]);
        if (!gpio_oe[pin].get_interface()) gpio_oe[pin].bind(m_gpio_oe_stubs[pin]);
    }
    for (unsigned int rail = 0; rail < NUM_RAILS; ++rail) {
        if (!rail_enabled[rail].get_interface()) rail_enabled[rail].bind(m_rail_enabled_stubs[rail]);
        if (!rail_voltage_uv[rail].get_interface()) rail_voltage_uv[rail].bind(m_rail_voltage_stubs[rail]);
    }
}

void tps6594::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    trans.set_dmi_allowed(false);
    if (!trans.get_data_ptr()) {
        trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
    }
    if (trans.get_byte_enable_ptr()) {
        trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        return;
    }
    if (trans.get_data_length() != 1 || trans.get_streaming_width() != 1) {
        trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
        return;
    }

    const uint64_t base = p_address.get_value() & 0x7f;
    // Linux maps the 16-bit register page to consecutive I2C aliases.
    if (trans.get_address() < base || trans.get_address() >= base + NUM_PAGES) {
        trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        return;
    }

    const uint8_t page = static_cast<uint8_t>(trans.get_address() - base);
    auto* extension = trans.get_extension<dw_i2c_extension>();
    const bool restart = extension && extension->restart;
    const bool stop = extension && extension->stop;
    if (!m_transaction_active || page != m_page || restart) {
        m_transaction_active = true;
        m_page = page;
        if (trans.get_command() == tlm::TLM_WRITE_COMMAND) m_expect_register = true;
    }

    switch (trans.get_command()) {
    case tlm::TLM_WRITE_COMMAND:
        if (m_expect_register) {
            m_pointer = *trans.get_data_ptr();
            m_expect_register = false;
        } else {
            write_register(m_page, m_pointer++, *trans.get_data_ptr());
        }
        break;
    case tlm::TLM_READ_COMMAND:
        *trans.get_data_ptr() = read_register(m_page, m_pointer++);
        break;
    default:
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    if (stop) {
        m_transaction_active = false;
        m_expect_register = true;
    }
    delay += p_access_latency.get_value();
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

void tps6594::reset_device()
{
    for (auto& page : m_registers) page.fill(0);

    // QBox baseline: generic Rev. B register-table reset values plus valid
    // minimum LDO selectors. Production values come from the selected NVM.
    m_registers[0][DEV_REV] = p_dev_rev.get_value();
    m_registers[0][NVM_CODE_1] = p_nvm_code_1.get_value();
    m_registers[0][NVM_CODE_2] = p_nvm_code_2.get_value();
    for (unsigned int buck = 0; buck < 5; ++buck) {
        m_registers[0][BUCK1_CTRL + buck * 2] = 0x22;
        m_registers[0][BUCK1_CTRL + buck * 2 + 1] = 0x22;
    }
    for (unsigned int ldo = 0; ldo < 4; ++ldo) m_registers[0][LDO1_CTRL + ldo] = 0x60;
    m_registers[0][LDO1_VOUT + 0] = 0x08; // LDO1: 0.6 V
    m_registers[0][LDO1_VOUT + 1] = 0x08; // LDO2: 0.6 V
    m_registers[0][LDO1_VOUT + 2] = 0x08; // LDO3: 0.6 V
    m_registers[0][LDO1_VOUT + 3] = 0x20; // LDO4: 1.2 V
    m_registers[0][0x2c] = 0x40;
    for (unsigned int pin = 0; pin < NUM_GPIOS; ++pin) m_registers[0][GPIO1_CONF + pin] = 0x0a;
    m_registers[0][MASK_GPIO1_8_FALL] = p_gpio_irq_mask.get_value() & 0xff;
    m_registers[0][MASK_GPIO1_8_RISE] = p_gpio_irq_mask.get_value() & 0xff;
    const uint8_t gpio9_11_mask = (p_gpio_irq_mask.get_value() >> 8) & 0x07;
    m_registers[0][MASK_GPIO9_11] = gpio9_11_mask | (gpio9_11_mask << 3);
    m_registers[0][0x3c] = 0x88;
    m_registers[0][0x7d] = 0xc0;
    m_registers[0][0x82] = 0x08;
    m_registers[0][RTC_STATUS] = RTC_POWER_UP;
    m_registers[4][0x02] = 0x30;
    m_registers[4][0x03] = 0x7f;
    m_registers[4][0x04] = 0x7f;
    m_registers[4][0x05] = 0xff;
    m_registers[4][0x06] = 0x02;
    m_registers[4][0x07] = 0x0a;
    m_registers[4][0x09] = 0xff;
    m_registers[4][0x0a] = 0x20;

    m_pointer = 0;
    m_page = 0;
    m_transaction_active = false;
    m_expect_register = true;
    m_rtc_running = false;
    m_rtc_base_seconds = 0;
    m_rtc_base_time = sc_core::sc_time_stamp();
    m_rtc_shadow.fill(0);
    m_int_n_level = !interrupt_pending();
}

uint8_t tps6594::read_register(unsigned int page, uint8_t reg)
{
    if (page != 0) return m_registers[page][reg];

    if (reg == GPIO_IN_1 || reg == GPIO_IN_2) return gpio_input_register(reg - GPIO_IN_1);
    if (reg >= RTC_SECONDS && reg <= RTC_WEEKS) {
        std::array<uint8_t, 7> now{};
        if (m_registers[0][RTC_CTRL_1] & RTC_GET_TIME) return m_rtc_shadow[reg - RTC_SECONDS];
        store_rtc(rtc_seconds_now(), now);
        return now[reg - RTC_SECONDS];
    }
    if (reg == RTC_STATUS) {
        return (m_registers[0][RTC_STATUS] & ~RTC_RUN) | (m_rtc_running ? RTC_RUN : 0);
    }
    if ((reg >= INT_TOP && reg <= INT_LDO_VMON) || reg == INT_GPIO || reg == INT_STARTUP) {
        return interrupt_summary(reg);
    }
    return m_registers[0][reg];
}

void tps6594::write_register(unsigned int page, uint8_t reg, uint8_t value)
{
    if (page != 0) {
        // Linux defaults to non-CRC I2C. Keep CRC disabled so an opt-in CRC
        // probe fails instead of silently using the wrong wire protocol.
        if (page == 1 && reg == (SERIAL_IF_CONFIG & 0xff)) value &= ~CRC_ENABLE;
        m_registers[page][reg] = value;
        return;
    }

    if (reg == DEV_REV || reg == NVM_CODE_1 || reg == NVM_CODE_2 || reg == GPIO_IN_1 || reg == GPIO_IN_2 ||
        reg == INT_TOP || reg == INT_BUCK || reg == INT_LDO_VMON) {
        return;
    }

    if ((reg >= INT_BUCK1_2 && reg <= INT_ESM) || reg == RTC_STATUS) {
        if (reg == INT_GPIO) value &= 0x07;
        if (reg == INT_STARTUP) value &= 0x33;
        if (reg == RTC_STATUS) value &= RTC_POWER_UP | RTC_ALARM | RTC_TIMER;
        m_registers[0][reg] &= ~value;
        update_interrupt();
        return;
    }

    const uint8_t old = m_registers[0][reg];
    m_registers[0][reg] = value;
    if ((reg >= GPIO1_CONF && reg < GPIO1_CONF + NUM_GPIOS) || reg == GPIO_OUT_1 || reg == GPIO_OUT_2) {
        m_output_event.notify();
    }
    if ((reg >= BUCK1_CTRL && reg <= BUCK1_CTRL + 9) || (reg >= BUCK1_VOUT_1 && reg <= BUCK1_VOUT_1 + 9) ||
        (reg >= LDO1_CTRL && reg < LDO1_CTRL + 4) || (reg >= LDO1_VOUT && reg < LDO1_VOUT + 4)) {
        m_rail_event.notify();
    }
    if (reg >= RTC_SECONDS && reg <= RTC_WEEKS) load_rtc_base();
    if (reg == RTC_CTRL_1) {
        if (!(old & RTC_GET_TIME) && (value & RTC_GET_TIME)) capture_rtc_shadow();
        update_rtc_running();
    } else if (reg == RTC_CTRL_2) {
        update_rtc_running();
    }
    if ((reg >= ALARM_SECONDS && reg <= ALARM_YEARS) || reg == RTC_INTERRUPTS) schedule_alarm();
}

void tps6594::set_gpio_input(unsigned int pin, bool level)
{
    const bool old = m_gpio_inputs[pin];
    if (old == level) return;
    m_gpio_inputs[pin] = level;
    m_output_event.notify(sc_core::SC_ZERO_TIME);

    const uint8_t conf = m_registers[0][GPIO1_CONF + pin];
    if ((conf & GPIO_SEL_MASK) || (conf & GPIO_DIR)) return;

    bool masked;
    if (pin < 8) {
        const uint8_t mask_reg = level ? MASK_GPIO1_8_RISE : MASK_GPIO1_8_FALL;
        masked = m_registers[0][mask_reg] & (1U << pin);
        if (!masked) m_registers[0][INT_GPIO1_8] |= 1U << pin;
    } else {
        const unsigned int bit = pin - 8;
        const unsigned int mask_bit = bit + (level ? 3 : 0);
        masked = m_registers[0][MASK_GPIO9_11] & (1U << mask_bit);
        if (!masked) m_registers[0][INT_GPIO] |= 1U << bit;
    }
    if (!masked) update_interrupt();
}

uint8_t tps6594::gpio_input_register(unsigned int bank) const
{
    uint8_t value = 0;
    const unsigned int first = bank * 8;
    for (unsigned int pin = first; pin < std::min(first + 8, NUM_GPIOS); ++pin) {
        if (gpio_output_level(pin)) value |= 1U << (pin - first);
    }
    return value;
}

bool tps6594::gpio_output_level(unsigned int pin) const
{
    const uint8_t conf = m_registers[0][GPIO1_CONF + pin];
    if (!(conf & GPIO_DIR)) return m_gpio_inputs[pin];

    const bool latch = m_registers[0][GPIO_OUT_1 + pin / 8] & (1U << (pin % 8));
    if (!(conf & GPIO_OD) || !latch) return latch;
    return m_gpio_inputs[pin] || (p_gpio_pullups.get_value() & (1U << pin));
}

void tps6594::drive_gpio_outputs()
{
    for (unsigned int pin = 0; pin < NUM_GPIOS; ++pin) {
        const uint8_t conf = m_registers[0][GPIO1_CONF + pin];
        const bool output = conf & GPIO_DIR;
        const bool latch = m_registers[0][GPIO_OUT_1 + pin / 8] & (1U << (pin % 8));
        gpio_out[pin]->write(gpio_output_level(pin));
        gpio_oe[pin]->write(output && (!(conf & GPIO_OD) || !latch));
    }
}

void tps6594::drive_rails()
{
    for (unsigned int rail = 0; rail < NUM_RAILS; ++rail) {
        uint8_t ctrl;
        uint8_t selector;
        if (rail < 5) {
            ctrl = m_registers[0][BUCK1_CTRL + rail * 2];
            selector = m_registers[0][BUCK1_VOUT_1 + rail * 2 + ((ctrl >> 3) & 1U)];
        } else {
            ctrl = m_registers[0][LDO1_CTRL + rail - 5];
            selector = m_registers[0][LDO1_VOUT + rail - 5];
        }
        rail_enabled[rail]->write(ctrl & RAIL_ENABLE);
        rail_voltage_uv[rail]->write((ctrl & RAIL_ENABLE) ? rail_voltage(rail, selector) : 0);
    }
}

void tps6594::drive_interrupt() { int_n->write(m_int_n_level); }

void tps6594::update_interrupt()
{
    const bool level = !interrupt_pending();
    if (level != m_int_n_level) {
        m_int_n_level = level;
        m_irq_event.notify();
    }
}

bool tps6594::interrupt_pending() const
{
    if (m_registers[0][INT_BUCK1_2] || m_registers[0][INT_BUCK3_4] || m_registers[0][INT_BUCK5] ||
        m_registers[0][INT_LDO1_2] || m_registers[0][INT_LDO3_4] || m_registers[0][INT_VMON] ||
        (m_registers[0][INT_GPIO] & 0x07) || m_registers[0][INT_GPIO1_8] || (m_registers[0][INT_STARTUP] & 0x33) ||
        m_registers[0][INT_MISC] || m_registers[0][INT_MODERATE_ERR] || m_registers[0][INT_SEVERE_ERR] ||
        m_registers[0][INT_FSM_ERR] || m_registers[0][INT_COMM_ERR] || m_registers[0][INT_READBACK_ERR] ||
        m_registers[0][INT_ESM]) {
        return true;
    }
    return m_registers[0][RTC_STATUS] & (RTC_POWER_UP | RTC_ALARM | RTC_TIMER);
}

uint8_t tps6594::interrupt_summary(uint8_t reg) const
{
    switch (reg) {
    case INT_TOP:
        return (!!interrupt_summary(INT_BUCK)) | (!!interrupt_summary(INT_LDO_VMON) << 1) |
               (!!interrupt_summary(INT_GPIO) << 2) | (!!interrupt_summary(INT_STARTUP) << 3) |
               (!!m_registers[0][INT_MISC] << 4) | (!!m_registers[0][INT_MODERATE_ERR] << 5) |
               (!!m_registers[0][INT_SEVERE_ERR] << 6) | (!!m_registers[0][INT_FSM_ERR] << 7);
    case INT_BUCK:
        return (!!m_registers[0][INT_BUCK1_2]) | (!!m_registers[0][INT_BUCK3_4] << 1) |
               (!!m_registers[0][INT_BUCK5] << 2);
    case INT_LDO_VMON:
        return (!!m_registers[0][INT_LDO1_2]) | (!!m_registers[0][INT_LDO3_4] << 1) | (!!m_registers[0][INT_VMON] << 2);
    case INT_GPIO:
        return (m_registers[0][INT_GPIO] & 0x07) | (!!m_registers[0][INT_GPIO1_8] << 3);
    case INT_STARTUP:
        return (m_registers[0][INT_STARTUP] & 0x33) |
               ((m_registers[0][RTC_STATUS] & (RTC_POWER_UP | RTC_ALARM | RTC_TIMER)) ? 0x04 : 0);
    default:
        return m_registers[0][reg];
    }
}

void tps6594::update_rtc_running()
{
    // The hardware synchronizes RUN to 32 kHz. Immediate LT visibility keeps
    // the same state transition while avoiding a wall-clock polling delay.
    const bool running = (m_registers[0][RTC_CTRL_1] & RTC_STOP) && (m_registers[0][RTC_CTRL_2] & RTC_XTAL_ENABLE);
    if (running == m_rtc_running) return;

    if (running) {
        load_rtc_base();
        m_rtc_base_time = sc_core::sc_time_stamp();
    } else {
        const uint64_t seconds = rtc_seconds_now();
        std::array<uint8_t, 7> now{};
        store_rtc(seconds, now);
        std::copy(now.begin(), now.end(), m_registers[0].begin() + RTC_SECONDS);
        m_rtc_base_seconds = seconds;
        m_rtc_base_time = sc_core::sc_time_stamp();
    }
    m_rtc_running = running;
    schedule_alarm();
}

uint64_t tps6594::rtc_seconds_now() const
{
    if (!m_rtc_running) return m_rtc_base_seconds;
    const auto elapsed = sc_core::sc_time_stamp() - m_rtc_base_time;
    return m_rtc_base_seconds + elapsed.value() / sc_core::sc_time(1, sc_core::SC_SEC).value();
}

void tps6594::load_rtc_base()
{
    m_rtc_base_seconds = decode_rtc(&m_registers[0][RTC_SECONDS]);
    m_rtc_base_time = sc_core::sc_time_stamp();
    schedule_alarm();
}

void tps6594::store_rtc(uint64_t seconds, std::array<uint8_t, 7>& target) const
{
    uint64_t days = seconds / 86400;
    uint64_t time = seconds % 86400;
    unsigned int year = 2000;
    while (days >= (leap_year(year) ? 366U : 365U)) days -= leap_year(year++) ? 366U : 365U;
    unsigned int month = 1;
    while (days >= month_days(year, month)) days -= month_days(year, month++);

    target[0] = binary_to_bcd(time % 60);
    target[1] = binary_to_bcd((time / 60) % 60);
    target[2] = binary_to_bcd(time / 3600);
    target[3] = binary_to_bcd(days + 1);
    target[4] = binary_to_bcd(month);
    target[5] = binary_to_bcd(year - 2000);
    target[6] = binary_to_bcd((6 + seconds / 86400) % 7);
}

uint64_t tps6594::decode_rtc(const uint8_t* regs) const
{
    unsigned int year = 2000 + bcd_to_binary(regs[5]);
    unsigned int month = bcd_to_binary(regs[4]);
    unsigned int day = bcd_to_binary(regs[3]);
    const unsigned int hour = bcd_to_binary(regs[2]);
    const unsigned int minute = bcd_to_binary(regs[1]);
    const unsigned int second = bcd_to_binary(regs[0]);
    if (month < 1 || month > 12 || day < 1 || day > month_days(year, month) || hour > 23 || minute > 59 ||
        second > 59) {
        return 0;
    }

    uint64_t days = 0;
    for (unsigned int y = 2000; y < year; ++y) days += leap_year(y) ? 366 : 365;
    for (unsigned int m = 1; m < month; ++m) days += month_days(year, m);
    days += day - 1;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

void tps6594::capture_rtc_shadow() { store_rtc(rtc_seconds_now(), m_rtc_shadow); }

void tps6594::schedule_alarm()
{
    m_alarm_event.cancel();
    if (!m_rtc_running || !(m_registers[0][RTC_INTERRUPTS] & RTC_IT_ALARM)) return;

    const uint64_t now = rtc_seconds_now();
    const uint64_t alarm = decode_rtc(&m_registers[0][ALARM_SECONDS]);
    if (alarm < now) return;
    m_alarm_event.notify(sc_core::sc_time(alarm - now, sc_core::SC_SEC));
}

void tps6594::alarm_fired()
{
    if (!m_rtc_running || !(m_registers[0][RTC_INTERRUPTS] & RTC_IT_ALARM)) return;
    m_registers[0][RTC_STATUS] |= RTC_ALARM;
    update_interrupt();
}

uint32_t tps6594::rail_voltage(unsigned int rail, uint8_t selector)
{
    if (rail < 5) {
        if (selector <= 0x0e) return 300000 + selector * 20000;
        if (selector <= 0x72) return 600000 + (selector - 0x0f) * 5000;
        if (selector <= 0xaa) return 1100000 + (selector - 0x73) * 10000;
        return 1660000 + (selector - 0xab) * 20000;
    }
    if (rail < 8) {
        const uint8_t value = (selector & 0x7e) >> 1;
        return value >= 4 && value <= 0x3a ? 600000 + (value - 4) * 50000 : 0;
    }
    const uint8_t value = selector & 0x7f;
    return value >= 0x20 && value <= 0x74 ? 1200000 + (value - 0x20) * 25000 : 0;
}

void module_register() { GSC_MODULE_REGISTER_C(tps6594); }
