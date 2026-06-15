/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cci/utils/broker.h>
#include <gtest/gtest.h>
#include <ports/initiator-signal-socket.h>
#include <reset_fanout.h>
#include <systemc>
#include <vector>

namespace {

class ResetSource : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(ResetSource);

    InitiatorSignalSocket<bool> reset;

    explicit ResetSource(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , reset("reset")
    {
        SC_THREAD(run);
    }

    void run()
    {
        wait(sc_core::sc_time(1, sc_core::SC_PS));
        reset->write(true);
        wait(sc_core::sc_time(4, sc_core::SC_PS));
        reset->write(false);
    }
};

class SameTimestampResetSource : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(SameTimestampResetSource);

    InitiatorSignalSocket<bool> reset;

    explicit SameTimestampResetSource(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , reset("reset")
    {
        SC_THREAD(run);
    }

    void run()
    {
        wait(sc_core::sc_time(1, sc_core::SC_PS));
        reset->write(true);
        reset->write(false);
    }
};

class ResetSink : public sc_core::sc_module
{
public:
    TargetSignalSocket<bool> reset;
    std::vector<bool> observed;

    explicit ResetSink(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , reset("reset")
    {
        reset.register_value_changed_cb([this](bool value) {
            observed.push_back(value);
        });
    }
};

} // namespace

TEST(ResetFanoutTest, BroadcastsResetValueToEveryTarget)
{
    reset_fanout dut("reset_fanout");
    ResetSource source("source");
    ResetSink sink0("sink0");
    ResetSink sink1("sink1");
    ResetSink sink2("sink2");

    source.reset.bind(dut.reset_in);
    dut.reset_out.bind(sink0.reset);
    dut.reset_out.bind(sink1.reset);
    dut.reset_out.bind(sink2.reset);

    SameTimestampResetSource pulse_source("pulse_source");
    reset_fanout pulse_dut("pulse_reset_fanout");
    ResetSink pulse_sink0("pulse_sink0");
    ResetSink pulse_sink1("pulse_sink1");

    pulse_source.reset.bind(pulse_dut.reset_in);
    pulse_dut.reset_out.bind(pulse_sink0.reset);
    pulse_dut.reset_out.bind(pulse_sink1.reset);

    sc_core::sc_start(sc_core::sc_time(10, sc_core::SC_PS));

    ASSERT_GE(sink0.observed.size(), 2u);
    EXPECT_EQ(sink0.observed, sink1.observed);
    EXPECT_EQ(sink0.observed, sink2.observed);
    EXPECT_TRUE(sink0.observed.front());
    EXPECT_FALSE(sink0.observed.back());

    ASSERT_EQ(pulse_sink0.observed, (std::vector<bool>{true, false}));
    EXPECT_EQ(pulse_sink0.observed, pulse_sink1.observed);
}

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
