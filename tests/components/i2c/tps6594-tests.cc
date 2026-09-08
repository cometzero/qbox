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
    sc_core::sc_signal<bool> buck1_enabled;
    sc_core::sc_signal<bool> ldo1_enabled;
    sc_core::sc_signal<uint32_t> buck1_uv;
    sc_core::sc_signal<uint32_t> ldo1_uv;

    explicit Tps6594Bench(const sc_core::sc_module_name& name)
        : TestBench(name)
        , master("master")
        , bus("bus")
        , eeprom("eeprom")
        , pmic("pmic")
        , int_n("int_n")
        , gpio0_oe("gpio0_oe")
        , gpio8_oe("gpio8_oe")
        , buck1_enabled("buck1_enabled")
        , ldo1_enabled("ldo1_enabled")
        , buck1_uv("buck1_uv")
        , ldo1_uv("ldo1_uv")
    {
        master.bind(bus.target_socket);
        bus.initiator_socket.bind(eeprom.i2c_socket);
        bus.initiator_socket.bind(pmic.i2c_socket);
        pmic.int_n.bind(int_n);
        pmic.gpio_out[0].bind(pmic.gpio_in[1]);
        pmic.gpio_out[8].bind(pmic.gpio_in[9]);
        pmic.gpio_oe[0].bind(gpio0_oe);
        pmic.gpio_oe[8].bind(gpio8_oe);
        pmic.rail_enabled[0].bind(buck1_enabled);
        pmic.rail_enabled[5].bind(ldo1_enabled);
        pmic.rail_voltage_uv[0].bind(buck1_uv);
        pmic.rail_voltage_uv[5].bind(ldo1_uv);
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
    EXPECT_TRUE(buck1_enabled.read());
    EXPECT_EQ(buck1_uv.read(), 1100000U);
    write_reg(tps6594::LDO1_CTRL, 0x61);
    EXPECT_TRUE(ldo1_enabled.read());
    EXPECT_EQ(ldo1_uv.read(), 600000U);
    write_reg(tps6594::LDO1_VOUT, 0x38);
    EXPECT_EQ(ldo1_uv.read(), 1800000U);
    write_reg(tps6594::BUCK1_CTRL, 0x22);
    write_reg(tps6594::LDO1_CTRL, 0x60);
    EXPECT_FALSE(buck1_enabled.read());
    EXPECT_FALSE(ldo1_enabled.read());
    EXPECT_EQ(buck1_uv.read(), 0U);
    EXPECT_EQ(ldo1_uv.read(), 0U);

    uint8_t data = 0;
    EXPECT_EQ(byte(0x4d, tlm::TLM_READ_COMMAND, data, false, true), tlm::TLM_ADDRESS_ERROR_RESPONSE);
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
