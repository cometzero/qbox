/* SPDX-License-Identifier: BSD-3-Clause */

#include <array>
#include <cstdint>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <cci/utils/broker.h>
#include <tests/test-bench.h>

#include "dw-apb-i2c.h"
#include "i2c-bus.h"
#include "tps6594.h"

class Tps6594Bench : public TestBench
{
protected:
    tlm_utils::simple_initiator_socket<Tps6594Bench> master;
    i2c_bus bus;
    dw_i2c_eeprom eeprom;
    tps6594 pmic;
    sc_core::sc_signal<bool> int_n;
    sc_core::sc_signal<bool> gpio0_oe;
    sc_core::sc_signal<bool> gpio8_oe;
    sc_core::sc_vector<sc_core::sc_signal<bool>> rail_enabled;
    sc_core::sc_vector<sc_core::sc_signal<uint32_t>> rail_uv;

    explicit Tps6594Bench(const sc_core::sc_module_name& name)
        : TestBench(name)
        , master("master")
        , bus("bus")
        , eeprom("eeprom")
        , pmic("pmic")
        , int_n("int_n")
        , gpio0_oe("gpio0_oe")
        , gpio8_oe("gpio8_oe")
        , rail_enabled("rail_enabled", tps6594::NUM_RAILS)
        , rail_uv("rail_uv", tps6594::NUM_RAILS)
    {
        master.bind(bus.target_socket);
        bus.initiator_socket.bind(eeprom.i2c_socket);
        bus.initiator_socket.bind(pmic.i2c_socket);
        pmic.int_n.bind(int_n);
        pmic.gpio_out[0].bind(pmic.gpio_in[1]);
        pmic.gpio_out[8].bind(pmic.gpio_in[9]);
        pmic.gpio_oe[0].bind(gpio0_oe);
        pmic.gpio_oe[8].bind(gpio8_oe);
        for (unsigned rail = 0; rail < tps6594::NUM_RAILS; ++rail) {
            pmic.rail_enabled[rail].bind(rail_enabled[rail]);
            pmic.rail_voltage_uv[rail].bind(rail_uv[rail]);
        }
    }

    tlm::tlm_response_status byte(uint8_t address, tlm::tlm_command command, uint8_t& data, bool restart, bool stop)
    {
        tlm::tlm_generic_payload trans;
        dw_i2c_extension extension;
        extension.restart = restart;
        extension.stop = stop;
        trans.set_extension(&extension);
        trans.set_command(command);
        trans.set_address(address);
        trans.set_data_ptr(&data);
        trans.set_data_length(1);
        trans.set_streaming_width(1);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        master->b_transport(trans, delay);
        trans.clear_extension(&extension);
        sc_core::wait(delay);
        return trans.get_response_status();
    }

    void write_reg(uint16_t reg, uint8_t value)
    {
        uint8_t pointer = reg;
        ASSERT_EQ(byte(0x48 + (reg >> 8), tlm::TLM_WRITE_COMMAND, pointer, false, false), tlm::TLM_OK_RESPONSE);
        ASSERT_EQ(byte(0x48 + (reg >> 8), tlm::TLM_WRITE_COMMAND, value, false, true), tlm::TLM_OK_RESPONSE);
        sc_core::wait(sc_core::SC_ZERO_TIME);
    }

    uint8_t read_reg(uint16_t reg)
    {
        uint8_t pointer = reg;
        EXPECT_EQ(byte(0x48 + (reg >> 8), tlm::TLM_WRITE_COMMAND, pointer, false, false), tlm::TLM_OK_RESPONSE);
        uint8_t value = 0;
        EXPECT_EQ(byte(0x48 + (reg >> 8), tlm::TLM_READ_COMMAND, value, true, true), tlm::TLM_OK_RESPONSE);
        return value;
    }

    template <size_t N>
    void write_bulk(uint16_t reg, const std::array<uint8_t, N>& values)
    {
        uint8_t pointer = reg;
        ASSERT_EQ(byte(0x48 + (reg >> 8), tlm::TLM_WRITE_COMMAND, pointer, false, false), tlm::TLM_OK_RESPONSE);
        for (size_t i = 0; i < N; ++i) {
            uint8_t value = values[i];
            ASSERT_EQ(byte(0x48 + (reg >> 8), tlm::TLM_WRITE_COMMAND, value, false, i + 1 == N), tlm::TLM_OK_RESPONSE);
        }
    }
};

TEST_BENCH(Tps6594Bench, PagesIdentityAndRails)
{
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_EQ(read_reg(tps6594::DEV_REV), 0x00);
    EXPECT_EQ(read_reg(tps6594::NVM_CODE_1), 0x00);
    EXPECT_EQ(read_reg(0x402), 0x30);
    EXPECT_EQ(read_reg(tps6594::LDO1_VOUT), 0x08);
    EXPECT_EQ(read_reg(tps6594::LDO1_VOUT + 3), 0x20);

    write_reg(tps6594::BUCK1_VOUT_1, 0x73);
    write_reg(tps6594::BUCK1_CTRL, 0x23);
    EXPECT_TRUE(rail_enabled[0].read());
    EXPECT_EQ(rail_uv[0].read(), 1100000U);
    write_reg(tps6594::LDO1_CTRL, 0x61);
    EXPECT_TRUE(rail_enabled[5].read());
    EXPECT_EQ(rail_uv[5].read(), 600000U);
    write_reg(tps6594::LDO1_VOUT, 0x38);
    EXPECT_EQ(rail_uv[5].read(), 1800000U);
    write_reg(tps6594::BUCK1_CTRL, 0x22);
    write_reg(tps6594::LDO1_CTRL, 0x60);
    EXPECT_FALSE(rail_enabled[0].read());
    EXPECT_FALSE(rail_enabled[5].read());
    EXPECT_EQ(rail_uv[0].read(), 0U);
    EXPECT_EQ(rail_uv[5].read(), 0U);

    uint8_t data = 0;
    EXPECT_EQ(byte(0x4d, tlm::TLM_READ_COMMAND, data, false, true), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

TEST_BENCH(Tps6594Bench, AllNineRailEnableAndVoltageOutputs)
{
    for (unsigned rail = 0; rail < tps6594::NUM_RAILS; ++rail) {
        const unsigned control = rail < 5 ? tps6594::BUCK1_CTRL + rail * 2 : tps6594::LDO1_CTRL + rail - 5;
        const unsigned vout = rail < 5 ? tps6594::BUCK1_VOUT_1 + rail * 2 : tps6594::LDO1_VOUT + rail - 5;
        write_reg(vout, rail < 5 ? 0x73 : 0x38);
        write_reg(control, 1);
        EXPECT_TRUE(rail_enabled[rail].read()) << rail;
        EXPECT_EQ(rail_uv[rail].read(), rail < 5 ? 1100000U : 1800000U) << rail;
        write_reg(control, 0);
        EXPECT_FALSE(rail_enabled[rail].read()) << rail;
        EXPECT_EQ(rail_uv[rail].read(), 0U) << rail;
    }
}

TEST_BENCH(Tps6594Bench, GpioLoopbackAndW1cInterrupt)
{
    pmic.p_gpio_pullups.set_cci_value(cci::cci_value(0x101));
    sc_core::wait(sc_core::SC_ZERO_TIME);
    write_reg(tps6594::RTC_STATUS, 0xff);
    EXPECT_TRUE(int_n.read());

    write_reg(tps6594::MASK_GPIO1_8_FALL, 0xff);
    write_reg(tps6594::MASK_GPIO1_8_RISE, 0xff);
    write_reg(tps6594::GPIO1_CONF, 0x0b);
    write_reg(tps6594::GPIO1_CONF + 1, 0x08);
    write_reg(tps6594::GPIO_OUT_1, 0x01);
    EXPECT_FALSE(gpio0_oe.read());
    EXPECT_EQ(read_reg(tps6594::GPIO_IN_1) & 0x01, 0x01);
    EXPECT_EQ(read_reg(tps6594::GPIO_IN_1) & 0x02, 0x02);
    EXPECT_TRUE(int_n.read());

    write_reg(tps6594::MASK_GPIO1_8_RISE, 0xfd);
    write_reg(tps6594::GPIO_OUT_1, 0x00);
    write_reg(tps6594::GPIO_OUT_1, 0x01);
    EXPECT_FALSE(int_n.read());
    EXPECT_EQ(read_reg(tps6594::INT_GPIO1_8) & 0x02, 0x02);
    EXPECT_EQ(read_reg(tps6594::INT_GPIO) & 0x08, 0x08);
    EXPECT_EQ(read_reg(tps6594::INT_TOP) & 0x04, 0x04);
    write_reg(tps6594::INT_GPIO1_8, 0x02);
    EXPECT_TRUE(int_n.read());

    write_reg(tps6594::GPIO1_CONF + 8, 0x0b);
    write_reg(tps6594::GPIO1_CONF + 9, 0x08);
    write_reg(tps6594::GPIO_OUT_2, 0x01);
    EXPECT_FALSE(gpio8_oe.read());
    EXPECT_EQ(read_reg(tps6594::GPIO_IN_2) & 0x02, 0x02);
    EXPECT_FALSE(int_n.read());
    write_reg(tps6594::INT_GPIO, 0x02);
    EXPECT_TRUE(int_n.read());
}

TEST_BENCH(Tps6594Bench, AllGpioDirectionsAndOutputReadback)
{
    for (unsigned pin = 0; pin < tps6594::NUM_GPIOS; ++pin) {
        const unsigned bank = pin / 8;
        const uint8_t mask = 1U << (pin % 8);
        write_reg(tps6594::GPIO1_CONF + pin, 0x01); // GPIO output, push-pull.
        write_reg(tps6594::GPIO_OUT_1 + bank, mask);
        EXPECT_EQ(read_reg(tps6594::GPIO_IN_1 + bank) & mask, mask) << pin;
        write_reg(tps6594::GPIO_OUT_1 + bank, 0);
        EXPECT_EQ(read_reg(tps6594::GPIO_IN_1 + bank) & mask, 0) << pin;
        write_reg(tps6594::GPIO1_CONF + pin, 0x00); // GPIO input.
        // Pins 1/9 are wired to outputs 0/8; drive those source inputs.
        const unsigned source = (pin == 1 || pin == 9) ? pin - 1 : pin;
        pmic.gpio_in[source]->write(true);
        sc_core::wait(sc_core::SC_ZERO_TIME);
        EXPECT_EQ(read_reg(tps6594::GPIO_IN_1 + bank) & mask, mask) << pin;
        pmic.gpio_in[source]->write(false);
        sc_core::wait(sc_core::SC_ZERO_TIME);
        EXPECT_EQ(read_reg(tps6594::GPIO_IN_1 + bank) & mask, 0) << pin;
    }
}

TEST_BENCH(Tps6594Bench, RtcTicksShadowsAndAlarms)
{
    sc_core::wait(sc_core::SC_ZERO_TIME);
    write_reg(tps6594::RTC_STATUS, 0xff);
    const std::array<uint8_t, 7> time = { 0x58, 0x59, 0x23, 0x28, 0x02, 0x24, 0x03 };
    write_bulk(tps6594::RTC_SECONDS, time);
    write_reg(tps6594::RTC_CTRL_2, 0x01);
    write_reg(tps6594::RTC_CTRL_1, 0x01);
    EXPECT_EQ(read_reg(tps6594::RTC_STATUS) & 0x02, 0x02);

    sc_core::wait(sc_core::sc_time(2, sc_core::SC_SEC));
    write_reg(tps6594::RTC_CTRL_1, 0x00);
    write_reg(tps6594::RTC_CTRL_1, 0x40);
    EXPECT_EQ(read_reg(tps6594::RTC_SECONDS), 0x00);
    EXPECT_EQ(read_reg(tps6594::RTC_MINUTES), 0x00);
    EXPECT_EQ(read_reg(tps6594::RTC_HOURS), 0x00);
    EXPECT_EQ(read_reg(tps6594::RTC_DAYS), 0x29);

    const std::array<uint8_t, 7> alarm_base = { 0x00, 0x00, 0x00, 0x01, 0x03, 0x24, 0x05 };
    write_reg(tps6594::RTC_CTRL_1, 0x00);
    write_bulk(tps6594::RTC_SECONDS, alarm_base);
    const std::array<uint8_t, 6> alarm = { 0x02, 0x00, 0x00, 0x01, 0x03, 0x24 };
    write_bulk(tps6594::ALARM_SECONDS, alarm);
    write_reg(tps6594::RTC_INTERRUPTS, 0x08);
    write_reg(tps6594::RTC_CTRL_1, 0x01);
    sc_core::wait(sc_core::sc_time(2, sc_core::SC_SEC));
    EXPECT_FALSE(int_n.read());
    EXPECT_EQ(read_reg(tps6594::RTC_STATUS) & 0x40, 0x40);
    EXPECT_EQ(read_reg(tps6594::INT_STARTUP) & 0x04, 0x04);
    write_reg(tps6594::RTC_STATUS, 0xbf);
    EXPECT_FALSE(int_n.read());
    write_reg(tps6594::RTC_STATUS, 0xff);
    EXPECT_TRUE(int_n.read());
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker broker{};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
