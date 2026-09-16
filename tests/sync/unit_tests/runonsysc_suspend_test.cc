/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <gtest/gtest.h>
#include <runonsysc.h>
#include <pre_suspending_sc_support.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace sc_core;

TEST(runonsysc, timed_job_respects_other_initiator_barrier)
{
    gs::runonsysc dispatch("dispatch");
    gs::async_event release_barrier;
    sc_event completed;
    std::atomic<bool> job_done{false};
    std::mutex mutex;
    std::condition_variable started;
    bool job_started = false;
    bool completed_before_release = false;

    sc_spawn([&] {
        // Represent a different initiator which has not advanced its clock.
        sc_suspend_all();
        wait(release_barrier);
        sc_unsuspend_all();
        if (!job_done.load()) wait(completed);
        sc_stop();
    });

    std::thread initiator([&] {
        dispatch.run_on_sysc([&] {
            {
                std::lock_guard<std::mutex> lock(mutex);
                job_started = true;
                started.notify_one();
            }
            // A target may consume the request's annotated delay.
            wait(100, SC_NS);
            job_done.store(true);
            completed.notify(SC_ZERO_TIME);
        }, false, gs::runonsysc::Suspension::Respect);
        {
            std::unique_lock<std::mutex> lock(mutex);
            started.wait(lock, [&] { return job_started; });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        completed_before_release = job_done.load();
        release_barrier.async_notify();
    });
    sc_start();
    initiator.join();
    EXPECT_FALSE(completed_before_release);
    EXPECT_TRUE(job_done.load());
    EXPECT_EQ(sc_time_stamp(), sc_time(100, SC_NS));
}

int sc_main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
