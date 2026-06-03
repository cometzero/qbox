/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <host_gtimer.h>
#include <systemc>
#include <tlm>

namespace {

constexpr uint64_t PCTL = 0x000;
constexpr uint64_t FRQ = 0x010;
constexpr uint64_t P_CTL = 0x02c;

uint32_t access32(host_gtimer& dut, uint64_t offset, tlm::tlm_command command,
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

uint32_t read32(host_gtimer& dut, uint64_t offset)
{
    return access32(dut, offset, tlm::TLM_READ_COMMAND);
}

void write32(host_gtimer& dut, uint64_t offset, uint32_t value)
{
    (void)access32(dut, offset, tlm::TLM_WRITE_COMMAND, value);
}

} // namespace

TEST(HostGtimerTest, CounterBaseLowWordAdvances)
{
    host_gtimer dut("host_gtimer_counter");
    dut.p_counter_base = true;
    dut.p_counter_increment = 125u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, PCTL), 125u);
    EXPECT_EQ(read32(dut, PCTL), 250u);
}

TEST(HostGtimerTest, CounterBaseReportsFrequency)
{
    host_gtimer dut("host_gtimer_frequency");
    dut.p_counter_base = true;
    dut.p_frequency = 1000000000u;
    dut.before_end_of_elaboration();

    EXPECT_EQ(read32(dut, FRQ), 1000000000u);
}

TEST(HostGtimerTest, RegistersPreserveWrites)
{
    host_gtimer dut("host_gtimer_rw");

    write32(dut, P_CTL, 0x00000003u);

    EXPECT_EQ(read32(dut, P_CTL), 0x00000003u);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
