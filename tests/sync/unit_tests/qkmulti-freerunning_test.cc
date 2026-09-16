/* SPDX-License-Identifier: BSD-3-Clause */

#include <gtest/gtest.h>
#include <qkmulti-freerunning.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace sc_core;

class KeeperProbe : public gs::tlm_quantumkeeper_freerunning
{
public:
    void demand_progress()
    {
        request_progress();
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (status == RUNNING) refresh_progress_locked();
    }
    void refresh_without_demand()
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (status == RUNNING) refresh_progress_locked();
    }
};

static KeeperProbe* probe;
static KeeperProbe* owner;
static KeeperProbe* waiter;

TEST(freerunning, refresh_respects_request_and_completion_boundaries)
{
    sc_time clock(100, SC_NS);
    probe->set_progress_source([&] { return clock; });
    probe->start();
    probe->reset();
    probe->demand_progress();
    EXPECT_EQ(probe->get_current_time(), clock);

    clock = sc_time(150, SC_NS);
    probe->refresh_without_demand();
    EXPECT_EQ(probe->get_current_time(), sc_time(100, SC_NS));
    clock = sc_time(100, SC_NS);

    const auto token = probe->begin_request(clock);
    clock = sc_time(200, SC_NS);
    probe->demand_progress();
    probe->set(sc_time(500, SC_NS)); // A periodic deadline cannot skip MMIO.
    EXPECT_EQ(probe->get_current_time(), sc_time(100, SC_NS));
    probe->complete_request(token, sc_time(120, SC_NS));
    probe->demand_progress();
    EXPECT_EQ(probe->get_current_time(), sc_time(120, SC_NS));
    probe->resume_request(token);
    probe->demand_progress();
    EXPECT_EQ(probe->get_current_time(), clock);

    const auto stopped = probe->begin_request(clock);
    probe->stop();
    probe->complete_request(stopped, sc_time(900, SC_NS));
    probe->resume_request(stopped);
    EXPECT_EQ(probe->status.load(), gs::tlm_quantumkeeper_multithread::STOPPED);
    EXPECT_EQ(probe->get_current_time(), clock);
    probe->start();
    probe->reset();
    const auto reset = probe->begin_request(sc_time(20, SC_NS));
    probe->reset();
    probe->complete_request(reset, sc_time(900, SC_NS));
    EXPECT_EQ(probe->get_current_time(), sc_time_stamp());
    // A loosely-timed target may receive service before the annotated issue
    // time. It owns progress until publishing its annotated completion.
    const auto annotated = probe->begin_request(sc_time(20, SC_NS));
    probe->request_servicing(annotated);
    EXPECT_EQ(sc_time_stamp(), SC_ZERO_TIME);
    probe->complete_request(annotated, sc_time(25, SC_NS));
    EXPECT_EQ(probe->get_current_time(), sc_time(25, SC_NS));
    probe->resume_request(annotated);
    probe->stop();
    probe->set_progress_source({});
}

TEST(freerunning, undershot_goals_wait_for_native_clock_without_idle_polling)
{
    int later_requester, middle_requester;
    sc_time clock(10, SC_NS);
    std::vector<sc_time> wakeups;
    unsigned int samples = 0;
    probe->set_progress_source([&] { ++samples; return clock; });
    probe->set_progress_wakeup([&](sc_time goal) { wakeups.push_back(goal); });
    probe->start();
    probe->reset();
    probe->request_progress(sc_time(20, SC_NS));
    probe->refresh_without_demand();
    EXPECT_EQ(probe->get_current_time(), sc_time(10, SC_NS));
    ASSERT_EQ(wakeups.size(), 1u);
    EXPECT_EQ(wakeups.back(), sc_time(20, SC_NS));

    clock = sc_time(15, SC_NS);
    probe->refresh_without_demand();
    EXPECT_EQ(samples, 1u); // No busy resampling while the goal is unmet.
    probe->request_progress(sc_time(500, SC_NS), &later_requester);
    probe->refresh_without_demand();
    EXPECT_EQ(wakeups.back(), sc_time(20, SC_NS));
    probe->request_progress(sc_time(100, SC_NS), &middle_requester);
    probe->refresh_without_demand();
    EXPECT_EQ(wakeups.back(), sc_time(20, SC_NS));

    clock = sc_time(20, SC_NS);
    probe->progress_ready();
    probe->refresh_without_demand();
    EXPECT_EQ(probe->get_current_time(), clock);
    EXPECT_EQ(wakeups.back(), sc_time(100, SC_NS));
    clock = sc_time(100, SC_NS);
    probe->progress_ready();
    probe->refresh_without_demand();
    EXPECT_EQ(probe->get_current_time(), clock);
    EXPECT_EQ(wakeups.back(), sc_time(500, SC_NS));
    const auto armed = wakeups.size();
    clock = sc_time(500, SC_NS);
    probe->progress_ready();
    probe->refresh_without_demand();
    EXPECT_EQ(probe->get_current_time(), clock);
    EXPECT_EQ(wakeups.size(), armed);
    const auto finished_samples = samples;
    clock = sc_time(900, SC_NS);
    probe->progress_ready(); // An already-fired duplicate is harmless.
    probe->refresh_without_demand();
    EXPECT_EQ(samples, finished_samples);
    EXPECT_EQ(probe->get_current_time(), sc_time(500, SC_NS));

    probe->request_progress(sc_time(1000, SC_NS));
    probe->refresh_without_demand();
    probe->stop();
    probe->progress_ready();
    probe->request_progress(sc_time(2000, SC_NS));
    probe->start();
    probe->refresh_without_demand();
    EXPECT_EQ(probe->get_current_time(), clock);
    probe->reset();
    clock = sc_time(10, SC_NS);
    probe->request_progress(sc_time(20, SC_NS));
    probe->refresh_without_demand();
    EXPECT_EQ(wakeups.back(), sc_time(20, SC_NS));
    probe->reset();
    clock = sc_time(2000, SC_NS);
    probe->progress_ready();
    probe->refresh_without_demand();
    EXPECT_EQ(probe->get_current_time(), sc_time_stamp());
    probe->stop();
    probe->set_progress_source({});
    probe->set_progress_wakeup({});
}

TEST(freerunning, completed_and_replaced_requests_do_not_accumulate_goals)
{
    int requester;
    sc_time clock(10, SC_NS), wake;
    probe->start();
    probe->reset();
    probe->set_progress_source([&] { return clock; });
    probe->set_progress_wakeup([&](sc_time goal) { wake = goal; });
    for (unsigned int t = 20; t <= 1000; ++t)
        probe->request_progress(sc_time(t, SC_NS), &requester);
    EXPECT_NE(probe->get_status_json().find("\"progress_goals\":1"), std::string::npos);
    probe->refresh_without_demand();
    EXPECT_EQ(wake, sc_time(1000, SC_NS));
    probe->cancel_progress(&requester);
    clock = sc_time(2000, SC_NS);
    probe->progress_ready();
    probe->refresh_without_demand();
    EXPECT_EQ(probe->get_current_time(), sc_time(10, SC_NS));
    probe->stop();
    probe->set_progress_source({});
    probe->set_progress_wakeup({});
}

TEST(freerunning, io_wait_and_completion_preserve_timed_order)
{
    // A lock waiter must allow the current owner's timed target to finish.
    // Completion then constrains time until the native CPU acknowledges it.
    std::atomic<bool> completed{false};
    std::atomic<bool> later_event{false};
    std::atomic<bool> release_checked{false};
    std::atomic<bool> queued{false};
    std::atomic<bool> pending_event{false};
    std::atomic<bool> pending_checked{false};
    gs::async_event service_enqueue(false);
    unsigned int progress_samples = 0;
    bool service_finished = false;
    probe->reset();
    probe->set_progress_source([&] {
        ++progress_samples;
        return sc_time(100, SC_NS);
    });
    probe->start(); // Initially stale peer, advanced only on demand.
    probe->request_progress();
    owner->reset();
    waiter->reset();
    owner->start();
    waiter->start();
    const auto request = owner->begin_request(sc_time(10, SC_NS));
    const auto waiting = waiter->begin_request(sc_time(10, SC_NS));
    waiter->request_wait_io(waiting);
    std::thread native([&] {
        while (!queued.load(std::memory_order_acquire)) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        EXPECT_FALSE(pending_event.load(std::memory_order_acquire));
        pending_checked.store(true, std::memory_order_release);
        service_enqueue.async_notify();
        while (!completed.load(std::memory_order_acquire)) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        EXPECT_FALSE(later_event.load(std::memory_order_acquire));
        release_checked.store(true, std::memory_order_release);
        // This test's resumed CPU has no more work, so it becomes idle.
        owner->resume_request(request);
        owner->stop();
        waiter->request_io_acquired(waiting);
        waiter->complete_request(waiting, sc_time(40, SC_NS));
        waiter->resume_request(waiting);
        waiter->stop();
    });
    sc_spawn([&] {
        wait(10, SC_NS);
        queued.store(true, std::memory_order_release);
        wait(service_enqueue);
        owner->request_servicing(request);
        wait(20, SC_NS);
        EXPECT_EQ(sc_time_stamp(), sc_time(30, SC_NS));
        owner->complete_request(request, sc_time_stamp());
        completed.store(true, std::memory_order_release);
        service_finished = true;
    });
    sc_spawn([&] {
        wait(11, SC_NS);
        EXPECT_TRUE(pending_checked.load(std::memory_order_acquire));
        pending_event.store(true, std::memory_order_release);
    });
    sc_spawn([&] {
        wait(31, SC_NS);
        EXPECT_TRUE(release_checked.load(std::memory_order_acquire));
        later_event.store(true, std::memory_order_release);
    });
    sc_start(sc_time(50, SC_NS));
    native.join();
    EXPECT_TRUE(service_finished);
    EXPECT_TRUE(later_event.load());
    EXPECT_GT(progress_samples, 0u);
    probe->stop();
    probe->set_progress_source({});
}

TEST(freerunning, absolute_clock_publication_does_not_rebase_or_skip_requests)
{
    // Deliberately advance SystemC between sampling and publishing, without
    // depending on a native scheduling race to expose double accounting.
    const auto sampled_at = sc_time_stamp();
    const auto clock = sampled_at + sc_time(100, SC_NS);
    sc_start(sc_time(10, SC_NS));
    ASSERT_EQ(sc_time_stamp(), sampled_at + sc_time(10, SC_NS));
    probe->reset();
    probe->start();
    probe->publish_clock(clock);
    EXPECT_EQ(probe->get_current_time(), clock);
    probe->publish_clock(clock - sc_time(1, SC_NS));
    EXPECT_EQ(probe->get_current_time(), clock); // No backwards publication.

    const auto token = probe->begin_request(clock - sc_time(1, SC_NS));
    EXPECT_EQ(probe->get_current_time(), clock); // The actual pinned bound.
    probe->publish_clock(clock + sc_time(100, SC_NS));
    EXPECT_EQ(probe->get_current_time(), clock);
    probe->request_servicing(token);
    probe->publish_clock(clock + sc_time(100, SC_NS));
    EXPECT_EQ(probe->get_current_time(), clock);
    const auto completion = clock + sc_time(5, SC_NS);
    probe->complete_request(token, completion);
    probe->publish_clock(clock + sc_time(100, SC_NS));
    EXPECT_EQ(probe->get_current_time(), completion);
    probe->resume_request(token);
    probe->publish_clock(clock + sc_time(100, SC_NS));
    EXPECT_EQ(probe->get_current_time(), clock + sc_time(100, SC_NS));
    probe->stop();
    probe->publish_clock(clock + sc_time(200, SC_NS));
    EXPECT_EQ(probe->get_current_time(), clock + sc_time(100, SC_NS));
}

int sc_main(int argc, char** argv)
{
    tlm_utils::tlm_quantumkeeper::set_global_quantum(sc_time(10, SC_MS));
    probe = new KeeperProbe;
    owner = new KeeperProbe;
    waiter = new KeeperProbe;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
