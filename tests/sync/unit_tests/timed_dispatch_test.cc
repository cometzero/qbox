/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <gtest/gtest.h>
#include <timed_dispatch.h>
#include <functional>

using namespace sc_core;

TEST(timed_dispatch, offsets_use_the_transport_timestamp)
{
    bool done = false;
    sc_spawn([&] {
        for (bool consume : {false, true}) {
            const sc_time start = sc_time_stamp();
            const sc_time request = start + sc_time(20, SC_NS);
            sc_time completion;
            sc_time received;
            sc_time sample;
            sc_time update;
            bool in_dispatch = false;
            unsigned int samples = 0;

            gs::timed_dispatch(
                [&](const std::function<void()>& job) {
                    // Model kernel advancement during dispatch and while
                    // the external caller resumes after completion.
                    wait(5, SC_NS);
                    in_dispatch = true;
                    job();
                    in_dispatch = false;
                    wait(7, SC_NS);
                },
                [&] {
                    EXPECT_FALSE(in_dispatch);
                    ++samples;
                    sample = sc_time_stamp();
                    return request;
                },
                [&](sc_time& delay) {
                    received = sc_time_stamp() + delay;
                    if (consume) {
                        wait(delay);
                        delay = SC_ZERO_TIME;
                    }
                    // Both target timing styles may annotate extra latency.
                    delay += sc_time(8, SC_NS);
                },
                [&](const sc_time& delay) {
                    EXPECT_TRUE(in_dispatch);
                    update = sc_time_stamp();
                    completion = sc_time_stamp() + delay;
                });

            EXPECT_EQ(sample, start);
            EXPECT_EQ(samples, 1u);
            EXPECT_EQ(received, request);
            EXPECT_EQ(update, consume ? request : start + sc_time(5, SC_NS));
            EXPECT_EQ(completion, request + sc_time(8, SC_NS));
        }
        done = true;
    });
    sc_start();
    EXPECT_TRUE(done);
}

TEST(timed_dispatch, queued_request_does_not_chase_wall_clock)
{
    bool done = false;
    sc_spawn([&] {
        for (unsigned int queue_ns : {5u, 30u}) {
            const auto start = sc_time_stamp();
            sc_time clock = start + sc_time(20, SC_NS);
            unsigned int samples = 0;
            gs::timed_dispatch(
                [&](const std::function<void()>& job) {
                    wait(queue_ns, SC_NS);
                    clock = start + sc_time(50, SC_NS);
                    job();
                },
                [&] { ++samples; return clock; },
                [&](sc_time& delay) {
                    EXPECT_EQ(sc_time_stamp() + delay,
                              start + sc_time(std::max(20u, queue_ns), SC_NS));
                },
                [](const sc_time&) {});
            EXPECT_EQ(samples, 1u);
        }
        done = true;
    });
    sc_start();
    EXPECT_TRUE(done);
}

int sc_main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
