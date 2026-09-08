/* SPDX-License-Identifier: BSD-3-Clause */

#include <gtest/gtest.h>
#include <systemc>

#include <thread>

#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <signal-fault-injector.h>
#include <tests/test-bench.h>

class SignalFaultInjectorTest : public TestBench
{
protected:
    gs::signal_fault_injector injector;
    InitiatorSignalSocket<bool> source;
    InitiatorSignalSocket<bool> reset;
    TargetSignalSocket<bool> sink;
    std::thread::id systemc_thread;
    std::thread::id output_thread;

    void write_source(bool level)
    {
        source->write(level);
        sc_core::wait(sc_core::SC_ZERO_TIME);
        sc_core::wait(sc_core::SC_ZERO_TIME);
    }

    void write_reset(bool asserted)
    {
        reset->write(asserted);
        sc_core::wait(sc_core::SC_ZERO_TIME);
        sc_core::wait(sc_core::SC_ZERO_TIME);
    }

public:
    explicit SignalFaultInjectorTest(sc_core::sc_module_name name)
        : TestBench(name)
        , injector("injector")
        , source("source")
        , reset("reset_driver")
        , sink("sink")
        , systemc_thread(std::this_thread::get_id())
    {
        source.bind(injector.signal_in);
        reset.bind(injector.reset);
        injector.signal_out.bind(sink);
        sink.register_value_changed_cb([this](bool) { output_thread = std::this_thread::get_id(); });
    }
};

TEST_BENCH(SignalFaultInjectorTest, ActionsAndReset)
{
    std::thread external_source([this]() { source->write(true); });
    external_source.join();
    sc_core::wait(sc_core::SC_ZERO_TIME);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_TRUE(sink.read());
    EXPECT_EQ(output_thread, systemc_thread);
    write_source(false);
    EXPECT_FALSE(sink.read());

    ASSERT_TRUE(injector.arm("drop-next-assert"));
    write_source(true);
    EXPECT_FALSE(sink.read());
    EXPECT_EQ(injector.snapshot().match_count, 1u);
    write_source(true);
    EXPECT_FALSE(sink.read());
    write_source(false);
    write_source(true);
    EXPECT_TRUE(sink.read());

    ASSERT_TRUE(injector.arm("force-low"));
    EXPECT_FALSE(sink.read());
    write_source(false);
    write_source(true);
    EXPECT_FALSE(sink.read());
    ASSERT_TRUE(injector.clear());
    EXPECT_TRUE(sink.read());

    ASSERT_TRUE(injector.arm("force-high"));
    EXPECT_TRUE(sink.read());
    write_source(false);
    EXPECT_TRUE(sink.read());
    ASSERT_TRUE(injector.clear());
    EXPECT_FALSE(sink.read());

    ASSERT_TRUE(injector.arm("pulse", 20));
    EXPECT_TRUE(sink.read());
    EXPECT_TRUE(injector.snapshot().pulse_active);
    sc_core::wait(19, sc_core::SC_NS);
    EXPECT_TRUE(sink.read());
    sc_core::wait(1, sc_core::SC_NS);
    sc_core::wait(sc_core::SC_ZERO_TIME);
    EXPECT_FALSE(sink.read());

    ASSERT_TRUE(injector.arm("pulse", 20));
    EXPECT_TRUE(sink.read());
    sc_core::wait(5, sc_core::SC_NS);
    write_reset(true);
    EXPECT_FALSE(sink.read());
    EXPECT_EQ(injector.snapshot().action, "pass");
    sc_core::wait(20, sc_core::SC_NS);
    EXPECT_FALSE(sink.read());
    write_reset(false);

    EXPECT_FALSE(injector.arm("pulse"));
    EXPECT_FALSE(injector.arm("unsupported"));
}

int sc_main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
