/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <arm_system_counter.h>

#include <condition_variable>
#include <map>
#include <unordered_map>

namespace gs {

struct arm_system_counter::ObserverRegistry
{
    struct Slot {
        std::mutex mutex;
        std::condition_variable idle;
        std::function<void(uint64_t)> callback;
        uint64_t first_generation = 0;
        bool active = true;
        size_t in_flight = 0;
    };

    std::mutex mutex;
    std::unordered_map<uint64_t, std::shared_ptr<Slot> > slots;
    std::mutex delivery_mutex;
    std::map<uint64_t, bool> pending_generations;
    uint64_t next_id = 1;
    uint64_t next_generation = 1;
    bool closed = false;
    bool dispatching = false;

    static thread_local Slot* invoking;

    uint64_t add(std::function<void(uint64_t)> callback,
                 uint64_t first_generation);
    void remove(uint64_t id);
    void deliver(uint64_t generation);
    void notify(uint64_t generation);
    void close();
};

}
