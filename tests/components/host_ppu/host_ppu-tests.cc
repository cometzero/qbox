/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <systemc>
#include <tlm>
#include <cci/utils/broker.h>

#include <host_ppu.h>
#include <ports/target-signal-socket.h>

namespace {

constexpr uint64_t PPU_PWPR = 0x000;
constexpr uint64_t PPU_PMER = 0x004;
constexpr uint64_t PPU_PWSR = 0x008;
constexpr uint32_t PPU_PWPR_DYNAMIC_EN = 0x00000100;
constexpr uint32_t PPU_PWPR_OFF_LOCK_EN = 0x00001000;
constexpr uint32_t PPU_PWPR_OP_DYN_EN = 0x01000000;
constexpr uint32_t PPU_PWPR_OP_POLICY = 0x000f0000;
constexpr uint32_t PPU_PWSR_PWR_DYN_STATUS = 0x00000100;
constexpr uint32_t PPU_PWSR_OFF_LOCK_STATUS = 0x00001000;
constexpr uint32_t PPU_PWSR_OP_DYN_STATUS = 0x01000000;

class SignalSink : public sc_core::sc_module
{
public:
    TargetSignalSocket<bool> signal;
    std::vector<bool> values;

    explicit SignalSink(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , signal("signal")
    {
        signal.register_value_changed_cb([this](bool value) {
            values.push_back(value);
        });
    }
};

uint32_t access32(host_ppu& dut, uint64_t offset, tlm::tlm_command command,
                  uint32_t value = 0)
{
    tlm::tlm_generic_payload trans;
    auto data = value;

    trans.set_address(offset);
    trans.set_command(command);
    trans.set_data_length(sizeof(data));
    trans.set_streaming_width(sizeof(data));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    dut.b_transport(trans, delay);

    EXPECT_EQ(trans.get_response_status(), tlm::TLM_OK_RESPONSE);
    return data;
}

uint32_t read32(host_ppu& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(host_ppu& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(HostPpuTest, PowerPolicyWriteUpdatesPowerStatus)
{
    host_ppu dut("host_ppu");

    write32(dut, PPU_PWPR, 0x00000008u);
    EXPECT_EQ(read32(dut, PPU_PWSR) & 0xfu, 0x8u);

    write32(dut, PPU_PWPR, 0x00000005u);
    EXPECT_EQ(read32(dut, PPU_PWSR) & 0xfu, 0x5u);

    write32(dut, PPU_PWPR, 0x00000000u);
    EXPECT_EQ(read32(dut, PPU_PWSR) & 0xfu, 0x0u);
}

TEST(HostPpuTest, EmulatorRegisterIsWritable)
{
    host_ppu dut("host_ppu_pmer");

    write32(dut, PPU_PMER, 0x00000001u);
    EXPECT_EQ(read32(dut, PPU_PMER), 0x00000001u);
}

TEST(HostPpuTest, DynamicPolicyWriteUpdatesStatusBits)
{
    host_ppu dut("host_ppu_dynamic");

    write32(dut,
            PPU_PWPR,
            PPU_PWPR_DYNAMIC_EN | PPU_PWPR_OFF_LOCK_EN |
                PPU_PWPR_OP_DYN_EN | (0x5u << 16) | 0x8u);

    const auto status = read32(dut, PPU_PWSR);
    EXPECT_EQ(status & 0xfu, 0x8u);
    EXPECT_NE(status & PPU_PWSR_PWR_DYN_STATUS, 0u);
    EXPECT_NE(status & PPU_PWSR_OFF_LOCK_STATUS, 0u);
    EXPECT_NE(status & PPU_PWSR_OP_DYN_STATUS, 0u);
    EXPECT_EQ(status & PPU_PWPR_OP_POLICY, 0x00050000u);

    write32(dut, PPU_PWPR, 0x0u);
    EXPECT_EQ(read32(dut, PPU_PWSR) &
                  (PPU_PWSR_PWR_DYN_STATUS | PPU_PWSR_OFF_LOCK_STATUS |
                   PPU_PWSR_OP_DYN_STATUS | PPU_PWPR_OP_POLICY),
              0u);
}

TEST(HostPpuTest, PowerOnTransitionSignalsLoadBeforeResetRelease)
{
    auto broker = cci::cci_get_global_broker(cci::cci_originator("host_ppu_test"));
    broker.set_preset_cci_value("host_ppu_signal.assert_power_on_reset",
                                cci::cci_value(true));
    broker.set_preset_cci_value("host_ppu_signal.assert_power_on_load",
                                cci::cci_value(true));
    broker.set_preset_cci_value(
        "host_ppu_signal.power_on_reset_assert_on_power_off",
        cci::cci_value(true));
    broker.set_preset_cci_value("host_ppu_signal.power_on_load_pulse_width_ns",
                                cci::cci_value(1ull));
    broker.set_preset_cci_value("host_ppu_signal.power_on_load_to_reset_delay_ns",
                                cci::cci_value(1ull));

    host_ppu dut("host_ppu_signal");
    SignalSink reset_sink("host_ppu_reset_sink");
    SignalSink load_sink("host_ppu_load_sink");

    dut.power_on_reset.bind(reset_sink.signal);
    dut.power_on_load.bind(load_sink.signal);

    write32(dut, PPU_PWPR, 0x8u);
    sc_core::sc_start(sc_core::sc_time(3, sc_core::SC_NS));

    ASSERT_GE(load_sink.values.size(), 2u);
    EXPECT_TRUE(load_sink.values[0]);
    EXPECT_FALSE(load_sink.values[1]);
    ASSERT_GE(reset_sink.values.size(), 2u);
    EXPECT_FALSE(reset_sink.values.back());

    write32(dut, PPU_PWPR, 0x0u);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);

    ASSERT_GE(reset_sink.values.size(), 2u);
    EXPECT_TRUE(reset_sink.values.back());
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
