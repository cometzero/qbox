/* SPDX-License-Identifier: BSD-3-Clause */

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <systemc>
#include <cci/utils/broker.h>

#include <arm_system_counter.h>

namespace {

using Counter = gs::arm_system_counter;

TEST(ArmSystemCounter, DefaultApolloRateAdvancesOncePerEightNanoseconds)
{
    Counter counter("counter_default_rate");

    EXPECT_EQ(counter.count_at(0), 0u);
    EXPECT_EQ(counter.count_at(7), 0u);
    EXPECT_EQ(counter.count_at(8), 1u);
    EXPECT_EQ(counter.count_at(15), 1u);
    EXPECT_EQ(counter.count_at(16), 2u);
}

TEST(ArmSystemCounter, ReadsAreSideEffectFreeAndFollowInputEdges)
{
    Counter counter("counter");
    counter.set_input_frequency_at(125000000, 0);
    counter.set_integer_increment_at(8, 0);
    counter.set_reported_frequency_hz(125000000);

    EXPECT_EQ(counter.count_at(0), 0u);
    EXPECT_EQ(counter.count_at(7), 0u);
    EXPECT_EQ(counter.count_at(8), 8u);
    EXPECT_EQ(counter.count_at(8), 8u);
    EXPECT_EQ(counter.count_at(15), 8u);
    EXPECT_EQ(counter.count_at(16), 16u);

    const Counter::StateSnapshot state = counter.snapshot();
    EXPECT_EQ(state.input_frequency_hz, 125000000u);
    EXPECT_EQ(state.integer_increment(), 8u);
    EXPECT_EQ(state.reported_frequency_hz, 125000000u);
}

TEST(ArmSystemCounter, EnableAndHaltPreserveInputClockPhase)
{
    Counter counter("counter_phase");
    counter.set_input_frequency_at(125000000, 0);
    counter.set_integer_increment_at(8, 0);

    EXPECT_TRUE(counter.set_enabled_at(false, 13));
    EXPECT_EQ(counter.count_at(13), 8u);
    EXPECT_EQ(counter.snapshot().input_tick_remainder, 625000000u);

    EXPECT_TRUE(counter.set_enabled_at(true, 17));
    EXPECT_EQ(counter.count_at(23), 8u);
    EXPECT_EQ(counter.count_at(24), 16u);

    EXPECT_TRUE(counter.set_halt_on_debug_at(true, 25));
    EXPECT_TRUE(counter.set_debug_halted_at(true, 26));
    EXPECT_EQ(counter.count_at(40), 16u);
    EXPECT_TRUE(counter.set_debug_halted_at(false, 41));
    EXPECT_EQ(counter.count_at(47), 16u);
    EXPECT_EQ(counter.count_at(48), 24u);
}

TEST(ArmSystemCounter, FixedPointScaleCarriesFractionAcrossReanchor)
{
    constexpr uint32_t half_count_per_tick_8_24 = 0x00800000u;
    Counter counter("counter_fraction");
    counter.set_input_frequency_at(1000000000, 0);
    counter.set_scale_8_24_at(half_count_per_tick_8_24, 0);

    EXPECT_EQ(counter.count_at(1), 0u);
    EXPECT_EQ(counter.count_at(2), 1u);

    int64_t deadline = -1;
    EXPECT_TRUE(counter.deadline_ns(1, 0, deadline));
    EXPECT_EQ(deadline, 2);
    EXPECT_TRUE(counter.deadline_ns(3, 1, deadline));
    EXPECT_EQ(deadline, 6);

    counter.set_enabled_at(false, 3);
    EXPECT_EQ(counter.snapshot().fractional_count, 0x00800000u);
    counter.set_enabled_at(true, 5);
    EXPECT_EQ(counter.count_at(6), 2u);
}

TEST(ArmSystemCounter, DeadlineUsesFirstCrossingEdge)
{
    Counter counter("counter_deadline");
    counter.set_input_frequency_at(125000000, 0);
    counter.set_integer_increment_at(8, 0);

    int64_t deadline = -1;
    EXPECT_TRUE(counter.deadline_ns(8, 0, deadline));
    EXPECT_EQ(deadline, 8);
    EXPECT_TRUE(counter.deadline_ns(5, 0, deadline));
    EXPECT_EQ(deadline, 8);
    EXPECT_TRUE(counter.deadline_ns(8, 9, deadline));
    EXPECT_EQ(deadline, 9);
    EXPECT_TRUE(counter.deadline_ns(16, 9, deadline));
    EXPECT_EQ(deadline, 16);
}

TEST(ArmSystemCounter, DeadlineUsesModularGenericTimerOrdering)
{
    Counter counter("counter_wrap");
    counter.set_input_frequency_at(125000000, 0);
    counter.set_integer_increment_at(8, 0);

    int64_t deadline = -1;
    counter.reanchor_at(std::numeric_limits<uint64_t>::max() - 4, 0, 0);
    EXPECT_TRUE(counter.deadline_ns(3, 0, deadline));
    EXPECT_EQ(deadline, 8);
    EXPECT_TRUE(counter.deadline_ns(std::numeric_limits<uint64_t>::max() - 1,
                                    0, deadline));
    EXPECT_EQ(deadline, 8);
    EXPECT_TRUE(counter.deadline_ns(std::numeric_limits<uint64_t>::max() - 5,
                                    0, deadline));
    EXPECT_EQ(deadline, 0);

    counter.set_enabled_at(false, 0);
    EXPECT_FALSE(counter.deadline_ns(3, 0, deadline));
}

TEST(ArmSystemCounter, ReportedFrequencyDoesNotReanchorOrNotify)
{
    Counter counter("counter_reported");
    std::vector<uint64_t> generations;
    Counter::ObserverSubscription observer = counter.observe(
        [&counter, &generations](uint64_t generation) {
            generations.push_back(generation);
            EXPECT_GE(counter.snapshot().generation, generation);
        });

    const uint64_t generation = counter.snapshot().generation;
    counter.set_reported_frequency_hz(125000000);
    EXPECT_EQ(counter.snapshot().generation, generation);
    EXPECT_TRUE(generations.empty());

    EXPECT_TRUE(counter.set_integer_increment_at(4, 0));
    ASSERT_EQ(generations.size(), 1u);
    EXPECT_EQ(generations.front(), generation + 1);

    observer.reset();
    counter.set_enabled_at(false, 0);
    EXPECT_EQ(generations.size(), 1u);
}

TEST(ArmSystemCounter, ConcurrentMutationsNotifyInGenerationOrder)
{
    Counter counter("counter_concurrent_observers");
    std::mutex observed_mutex;
    std::vector<uint64_t> observed;
    Counter::ObserverSubscription observer = counter.observe(
        [&observed_mutex, &observed](uint64_t generation) {
            std::lock_guard<std::mutex> lock(observed_mutex);
            observed.push_back(generation);
        });

    std::vector<std::thread> writers;
    for (uint64_t value = 1; value <= 16; ++value) {
        writers.emplace_back([&counter, value] {
            counter.reanchor_at(value, 0, 0);
        });
    }
    for (std::thread& writer : writers) {
        writer.join();
    }

    std::vector<uint64_t> expected(16);
    std::iota(expected.begin(), expected.end(), 1);
    EXPECT_EQ(observed, expected);
}

TEST(ArmSystemCounter, ObserverStartsAfterRegistrationGeneration)
{
    Counter counter("counter_late_observer");
    counter.reanchor_at(1, 0, 0);

    std::vector<uint64_t> observed;
    Counter::ObserverSubscription observer = counter.observe(
        [&observed](uint64_t generation) { observed.push_back(generation); });
    counter.reanchor_at(2, 0, 0);

    ASSERT_EQ(observed.size(), 1u);
    EXPECT_EQ(observed.front(), 2u);
}

TEST(ArmSystemCounter, ResetRestoresConfiguredResetState)
{
    Counter counter("counter_reset");
    counter.set_input_frequency_at(125000000, 0);
    counter.set_integer_increment_at(8, 0);
    counter.set_reported_frequency_hz(125000000);
    counter.reanchor_at(0x1234, 0x00800000, 9);
    counter.set_debug_halted_at(true, 10);

    counter.reset_at(16);
    const Counter::StateSnapshot state = counter.snapshot();
    EXPECT_EQ(state.anchor_time_ns, 16);
    EXPECT_EQ(state.anchor_count, 0u);
    EXPECT_EQ(state.fractional_count, 0u);
    EXPECT_EQ(state.input_tick_remainder, 0u);
    EXPECT_EQ(state.input_frequency_hz, 125000000u);
    EXPECT_EQ(state.integer_increment(), 1u);
    EXPECT_EQ(state.reported_frequency_hz, 125000000u);
    EXPECT_TRUE(state.enabled);
    EXPECT_FALSE(state.debug_halted);
}

TEST(ArmSystemCounter, PartialWritesUseTheCountAtTheirEffectiveTimestamp)
{
    Counter counter("counter_partial_write");
    counter.reanchor_at(0x12345678fffffffeULL, 0, 0);

    counter.write_count_low_at(0xffffffffu, 8);
    EXPECT_EQ(counter.count_at(8), 0x12345678ffffffffULL);
    EXPECT_EQ(counter.count_at(16), 0x1234567900000000ULL);

    counter.write_count_high_at(0xa5a5a5a5u, 16);
    EXPECT_EQ(counter.count_at(16), 0xa5a5a5a500000000ULL);
}

TEST(ArmSystemCounter, FrozenStructureAllowsRuntimeControlTransitions)
{
    Counter counter("counter_frozen_mutations");
    std::vector<uint64_t> generations;
    Counter::ObserverSubscription observer = counter.observe(
        [&generations](uint64_t generation) {
            generations.push_back(generation);
        });
    counter.freeze_mutations();

    EXPECT_TRUE(counter.snapshot().mutations_frozen);
    EXPECT_FALSE(counter.set_enabled_at(true, 0));
    EXPECT_FALSE(counter.set_halt_on_debug_at(false, 0));
    EXPECT_FALSE(counter.set_control_at(true, false, 0));
    EXPECT_FALSE(counter.set_input_frequency_at(125000000, 0));
    EXPECT_THROW(counter.set_input_frequency_at(100000000, 0),
                 std::logic_error);
    EXPECT_THROW(counter.set_integer_increment_at(2, 0), std::logic_error);
    EXPECT_THROW(counter.set_scale_8_24_at(0x00800000u, 0),
                 std::logic_error);

    EXPECT_TRUE(counter.set_control_at(false, false, 8));
    EXPECT_EQ(counter.count_at(16), 1u);
    EXPECT_TRUE(counter.set_control_at(true, false, 16));
    EXPECT_EQ(counter.count_at(24), 2u);
    EXPECT_TRUE(counter.set_control_at(true, true, 24));
    EXPECT_TRUE(counter.set_debug_halted_at(true, 24));
    EXPECT_EQ(counter.count_at(32), 2u);
    EXPECT_TRUE(counter.set_debug_halted_at(false, 32));
    EXPECT_EQ(counter.count_at(40), 3u);
    EXPECT_TRUE(counter.reanchor_at(9, 0, 40));
    EXPECT_NO_THROW(counter.reset_at(48));
    EXPECT_EQ(counter.count_at(48), 0u);
    EXPECT_TRUE(counter.snapshot().mutations_frozen);
    EXPECT_THROW(counter.set_integer_increment_at(4, 48), std::logic_error);
    EXPECT_NO_THROW(counter.set_reported_frequency_hz(100000000));

    EXPECT_EQ(generations,
              (std::vector<uint64_t>{ 1, 2, 3, 4, 5, 6, 7 }));
}

TEST(ArmSystemCounter, SnapshotAtMaterializesOneImmutableState)
{
    Counter counter("counter_snapshot_at");
    counter.set_integer_increment_at(8, 0);

    const Counter::StateSnapshot evaluated = counter.snapshot_at(16);
    EXPECT_EQ(evaluated.anchor_time_ns, 16);
    EXPECT_EQ(evaluated.anchor_count, 16u);
    EXPECT_EQ(evaluated.input_frequency_hz, 125000000u);
    EXPECT_EQ(evaluated.reported_frequency_hz, 125000000u);
    EXPECT_TRUE(evaluated.enabled);
    EXPECT_FALSE(evaluated.debug_halted);

    EXPECT_EQ(counter.snapshot().anchor_time_ns, 0);
    EXPECT_EQ(counter.snapshot().anchor_count, 0u);
}

TEST(ArmSystemCounter, RejectsBackwardAndNegativeTimestamps)
{
    Counter counter("counter_time_validation");
    counter.reanchor_at(0, 0, 10);
    EXPECT_THROW(counter.count_at(9), std::out_of_range);
    EXPECT_THROW(counter.count_at(-1), std::out_of_range);
    EXPECT_THROW(counter.set_enabled_at(false, 9), std::out_of_range);
}

}

int sc_main(int argc, char** argv)
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
