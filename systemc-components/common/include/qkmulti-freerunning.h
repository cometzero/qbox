/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QKMULTI_FREERUNNING_H
#define QKMULTI_FREERUNNING_H

#include <qkmultithread.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>

namespace gs {
class tlm_quantumkeeper_freerunning : public tlm_quantumkeeper_multithread
{
    enum class Phase { RUNNING, PENDING, WAIT_IO, SERVICING, READY };
    Phase m_phase = Phase::RUNNING;
    uint64_t m_request = 0;
    std::function<sc_core::sc_time()> m_progress_source;
    std::function<void(sc_core::sc_time)> m_progress_wakeup;
    std::map<const void*, sc_core::sc_time> m_progress_goals;
    bool m_progress_requested = false;

    bool valid_request(uint64_t token) const
    {
        return token == m_request && status == RUNNING && m_phase != Phase::RUNNING;
    }

protected:
    void refresh_progress_locked() override
    {
        if (m_phase == Phase::RUNNING && m_progress_source && m_progress_requested) {
            m_progress_requested = false;
            // The source must be nonblocking and must not acquire BQL. This
            // lock serializes sampling with request publication, so a live
            // clock can never move an already-pending request into the future.
            set_absolute_locked(m_progress_source(), false);
            for (auto it = m_progress_goals.begin(); it != m_progress_goals.end();) {
                if (it->second <= get_current_time()) it = m_progress_goals.erase(it);
                else ++it;
            }
            if (!m_progress_goals.empty() && m_progress_wakeup) {
                auto earliest = m_progress_goals.begin()->second;
                for (const auto& goal : m_progress_goals) earliest = std::min(earliest, goal.second);
                m_progress_wakeup(earliest);
            }
        }
    }

    bool awaiting_external_completion_locked() const override
    {
        return m_phase == Phase::WAIT_IO || m_phase == Phase::SERVICING;
    }

    void invalidate_progress_locked() override
    {
        ++m_request;
        m_phase = Phase::RUNNING;
        m_progress_requested = false;
        m_progress_goals.clear();
    }

public:
    // Native clock samples are already absolute. Converting them to a delay
    // and adding a later SystemC timestamp would count intervening progress
    // twice. Outstanding requests retain their published boundary.
    void publish_clock(const sc_core::sc_time& time)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (status == RUNNING && m_phase == Phase::RUNNING)
            set_absolute_locked(time);
    }

    std::string get_status_json() override
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        auto json = tlm_quantumkeeper_multithread::get_status_json();
        json.pop_back();
        json += ",\"request_phase\":" + std::to_string(static_cast<unsigned int>(m_phase));
        json += ",\"progress_goals\":" + std::to_string(m_progress_goals.size());
        if (m_progress_source)
            json += ",\"source_time\":\"" + m_progress_source().to_string() + "\"";
        return json + "}";
    }

    // A CPU issuing MMIO requests progress to its absolute issue time. Keep
    // an undershot goal until a native clock wakeup permits another sample;
    // never continuously chase wall clock when no transaction needs progress.
    // A zero goal retains the one-shot nudge contract for existing callers.
    void request_progress(sc_core::sc_time goal = sc_core::SC_ZERO_TIME, const void* requester = nullptr)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (status != RUNNING) return;
        if (goal != sc_core::SC_ZERO_TIME) {
            m_progress_goals.erase(requester);
            if (goal <= get_current_time()) return;
            m_progress_goals.emplace(requester, goal);
        }
        m_progress_requested = true;
        m_tick.notify(sc_core::SC_ZERO_TIME);
    }

    void cancel_progress(const void* requester)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        m_progress_goals.erase(requester);
        if (m_progress_goals.empty()) m_progress_requested = false;
    }

    // The native timer callback does not publish time or start a keeper. A
    // stale callback after stop/reset finds no outstanding goal and is inert.
    void progress_ready()
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (status != RUNNING || m_progress_goals.empty()) return;
        m_progress_requested = true;
        m_tick.notify(sc_core::SC_ZERO_TIME);
    }

    // Called under the keeper lock: schedule a native absolute-time wakeup
    // without acquiring BQL or waiting for its callback. The earliest unmet
    // goal is supplied, so a later requester cannot postpone an earlier one.
    void set_progress_wakeup(std::function<void(sc_core::sc_time)> wakeup)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        m_progress_wakeup = std::move(wakeup);
    }

    // Only use for the wall-clock-backed, non-icount QEMU policy. Installation
    // does not change the global quantum or any other keeper policy.
    void set_progress_source(std::function<sc_core::sc_time()> source)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        m_progress_source = std::move(source);
    }

    uint64_t begin_request(const sc_core::sc_time& issue)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        ++m_request;
        if (status == RUNNING) {
            m_phase = Phase::PENDING;
            set_absolute_locked(issue);
        }
        return m_request;
    }

    // WAIT_IO is valid only after a failed try_lock established that another
    // initiator owns the serialized I/O operation. It is not an enqueue gap.
    void request_wait_io(uint64_t token)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (valid_request(token) && m_phase == Phase::PENDING) {
            m_phase = Phase::WAIT_IO;
            m_tick.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void request_io_acquired(uint64_t token)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (valid_request(token) && m_phase == Phase::WAIT_IO) {
            m_phase = Phase::PENDING;
            set_absolute_locked(sc_core::sc_time_stamp());
        }
    }

    // SystemC only, once the service callback owns the request. The target
    // retains the incoming annotation and may consume it or return a delay.
    void request_servicing(uint64_t token)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (valid_request(token) && m_phase == Phase::PENDING) {
            assert(is_sysc_thread());
            m_phase = Phase::SERVICING;
            m_tick.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void complete_request(uint64_t token, const sc_core::sc_time& completion)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (valid_request(token)) {
            m_phase = Phase::READY;
            set_absolute_locked(completion);
        }
    }

    void resume_request(uint64_t token)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (valid_request(token) && m_phase == Phase::READY) {
            m_phase = Phase::RUNNING;
            m_tick.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void set(const sc_core::sc_time& time) override
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (m_phase == Phase::RUNNING) tlm_quantumkeeper_multithread::set(time);
    }

    void inc(const sc_core::sc_time& time) override
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (m_phase == Phase::RUNNING) tlm_quantumkeeper_multithread::inc(time);
    }

    virtual void sync() override
    {
        if (is_sysc_thread()) {
            sc_core::sc_time t = get_local_time();
            m_tick.notify();
            sc_core::wait(t);
        } else {
            /* Wake up the SystemC thread if it's waiting for us to keep up */
            m_tick.notify();
        }
    }

    virtual bool need_sync() override { return false; }
};
} // namespace gs
#endif
