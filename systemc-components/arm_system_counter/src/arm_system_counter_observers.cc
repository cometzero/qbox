/* SPDX-License-Identifier: BSD-3-Clause */

#include "arm_system_counter_observers.h"

#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gs {

thread_local arm_system_counter::ObserverRegistry::Slot*
    arm_system_counter::ObserverRegistry::invoking = nullptr;

uint64_t arm_system_counter::ObserverRegistry::add(
    std::function<void(uint64_t)> callback, uint64_t first_generation)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (closed) {
        throw std::logic_error("observer registry is closed");
    }
    const uint64_t id = next_id++;
    std::shared_ptr<Slot> slot(new Slot);
    slot->callback = std::move(callback);
    slot->first_generation = first_generation;
    slots.emplace(id, std::move(slot));
    return id;
}

void arm_system_counter::ObserverRegistry::remove(uint64_t id)
{
    std::shared_ptr<Slot> slot;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = slots.find(id);
        if (found == slots.end()) {
            return;
        }
        slot = found->second;
        slots.erase(found);
    }

    std::unique_lock<std::mutex> slot_lock(slot->mutex);
    slot->active = false;
    if (invoking != slot.get()) {
        slot->idle.wait(slot_lock, [&slot] { return slot->in_flight == 0; });
    }
}

void arm_system_counter::ObserverRegistry::deliver(uint64_t generation)
{
    std::vector<std::shared_ptr<Slot> > current;
    {
        std::lock_guard<std::mutex> lock(mutex);
        current.reserve(slots.size());
        for (const auto& item : slots) {
            current.push_back(item.second);
        }
    }

    std::exception_ptr first_exception;
    for (const std::shared_ptr<Slot>& slot : current) {
        {
            std::lock_guard<std::mutex> slot_lock(slot->mutex);
            if (!slot->active || generation < slot->first_generation) {
                continue;
            }
            ++slot->in_flight;
        }

        Slot* previous = invoking;
        invoking = slot.get();
        try {
            slot->callback(generation);
        } catch (...) {
            if (!first_exception) {
                first_exception = std::current_exception();
            }
        }
        invoking = previous;

        {
            std::lock_guard<std::mutex> slot_lock(slot->mutex);
            --slot->in_flight;
            if (slot->in_flight == 0) {
                slot->idle.notify_all();
            }
        }
    }

    if (first_exception) {
        std::rethrow_exception(first_exception);
    }
}

void arm_system_counter::ObserverRegistry::notify(uint64_t generation)
{
    std::unique_lock<std::mutex> delivery_lock(delivery_mutex);
    pending_generations.emplace(generation, true);
    if (dispatching) {
        return;
    }
    dispatching = true;

    std::exception_ptr first_exception;
    while (true) {
        const auto ready = pending_generations.find(next_generation);
        if (ready == pending_generations.end()) {
            dispatching = false;
            break;
        }
        const uint64_t current_generation = ready->first;
        pending_generations.erase(ready);
        ++next_generation;
        delivery_lock.unlock();
        try {
            deliver(current_generation);
        } catch (...) {
            if (!first_exception) {
                first_exception = std::current_exception();
            }
        }
        delivery_lock.lock();
    }
    delivery_lock.unlock();

    if (first_exception) {
        std::rethrow_exception(first_exception);
    }
}

void arm_system_counter::ObserverRegistry::close()
{
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(mutex);
        closed = true;
        ids.reserve(slots.size());
        for (const auto& item : slots) {
            ids.push_back(item.first);
        }
    }
    for (uint64_t id : ids) {
        remove(id);
    }
}

arm_system_counter::ObserverSubscription::ObserverSubscription(
    const std::shared_ptr<ObserverRegistry>& registry, uint64_t id)
    : m_registry(registry), m_id(id)
{
}

arm_system_counter::ObserverSubscription::~ObserverSubscription() { reset(); }

arm_system_counter::ObserverSubscription::ObserverSubscription(
    ObserverSubscription&& other) noexcept
    : m_registry(std::move(other.m_registry)), m_id(other.m_id)
{
    other.m_id = 0;
}

arm_system_counter::ObserverSubscription&
arm_system_counter::ObserverSubscription::operator=(ObserverSubscription&& other) noexcept
{
    if (this != &other) {
        reset();
        m_registry = std::move(other.m_registry);
        m_id = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

void arm_system_counter::ObserverSubscription::reset()
{
    if (m_id == 0) {
        return;
    }
    if (std::shared_ptr<ObserverRegistry> registry = m_registry.lock()) {
        registry->remove(m_id);
    }
    m_id = 0;
    m_registry.reset();
}

}
