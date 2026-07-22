/* SPDX-License-Identifier: BSD-3-Clause */

#include <arm_system_counter.h>

#include "arm_system_counter_observers.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace gs {

arm_system_counter::arm_system_counter(const sc_core::sc_module_name& name)
    : sc_core::sc_module(name)
    , p_input_frequency_hz("input_frequency_hz", 125000000,
                           "Physical input clock frequency in Hz")
    , p_integer_increment("integer_increment", 1,
                          "Visible integer increment per input edge")
    , p_reported_frequency_hz("reported_frequency_hz", 125000000,
                              "Architected reported frequency in Hz")
    , p_enabled("enabled", true, "Counter enable reset value")
    , p_halt_on_debug("halt_on_debug", false,
                      "Debug halt control reset value")
    , p_initial_count("initial_count", 0, "Counter reset value")
    , m_observers(new ObserverRegistry)
{
    if (p_input_frequency_hz.get_value() == 0 ||
        p_reported_frequency_hz.get_value() == 0 ||
        p_integer_increment.get_value() >
            (std::numeric_limits<uint64_t>::max() >> fractional_bits)) {
        throw std::invalid_argument("invalid arm_system_counter configuration");
    }

    m_state.anchor_count = p_initial_count.get_value();
    m_state.input_frequency_hz = p_input_frequency_hz.get_value();
    m_state.increment_8_24 = p_integer_increment.get_value() << fractional_bits;
    m_state.enabled = p_enabled.get_value();
    m_state.halt_on_debug = p_halt_on_debug.get_value();
    m_state.reported_frequency_hz = p_reported_frequency_hz.get_value();
    m_reset_state = m_state;
}

arm_system_counter::~arm_system_counter() { m_observers->close(); }

void arm_system_counter::require_mutable(const StateSnapshot& state)
{
    if (state.mutations_frozen) {
        throw std::logic_error(
            "arm_system_counter structural mutation is frozen");
    }
}

arm_system_counter::StateSnapshot arm_system_counter::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

uint64_t arm_system_counter::count_at(int64_t absolute_ns) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return count_at(m_state, absolute_ns);
}

uint64_t arm_system_counter::count_at(const sc_core::sc_time& time) const
{
    return count_at(absolute_ns(time));
}

bool arm_system_counter::deadline_ns(uint64_t target_count, int64_t from_ns,
                                     int64_t& deadline) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return deadline_ns(m_state, target_count, from_ns, deadline);
}

arm_system_counter::ObserverSubscription arm_system_counter::observe(
    std::function<void(uint64_t)> observer)
{
    if (!observer) {
        throw std::invalid_argument("observer callback is empty");
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return ObserverSubscription(
        m_observers,
        m_observers->add(std::move(observer), m_state.generation + 1));
}

bool arm_system_counter::mutate_at(
    int64_t absolute_ns, const std::function<bool(StateSnapshot&)>& mutation,
    bool structural)
{
    uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        validate_time(m_state, absolute_ns);
        StateSnapshot changed = m_state;
        if (!mutation(changed)) {
            return false;
        }
        if (structural) {
            require_mutable(m_state);
        }
        materialize(m_state, absolute_ns);
        mutation(m_state);
        generation = ++m_state.generation;
    }
    notify_observers(generation);
    return true;
}

bool arm_system_counter::set_input_frequency_at(uint64_t frequency_hz,
                                                int64_t absolute_ns)
{
    if (frequency_hz == 0) {
        throw std::invalid_argument("input frequency must be nonzero");
    }
    return mutate_at(absolute_ns, [frequency_hz](StateSnapshot& state) {
        if (state.input_frequency_hz == frequency_hz) {
            return false;
        }
        state.input_frequency_hz = frequency_hz;
        return true;
    }, true);
}

bool arm_system_counter::set_integer_increment_at(uint64_t increment,
                                                  int64_t absolute_ns)
{
    if (increment > (std::numeric_limits<uint64_t>::max() >> fractional_bits)) {
        throw std::invalid_argument("integer increment exceeds fixed-point range");
    }
    const uint64_t scale = increment << fractional_bits;
    return mutate_at(absolute_ns, [scale](StateSnapshot& state) {
        if (state.increment_8_24 == scale) {
            return false;
        }
        state.increment_8_24 = scale;
        return true;
    }, true);
}

bool arm_system_counter::set_scale_8_24_at(uint32_t scale,
                                           int64_t absolute_ns)
{
    return mutate_at(absolute_ns, [scale](StateSnapshot& state) {
        if (state.increment_8_24 == scale) {
            return false;
        }
        state.increment_8_24 = scale;
        return true;
    }, true);
}

bool arm_system_counter::reanchor_at(uint64_t count,
                                     uint32_t fractional_count,
                                     int64_t absolute_ns)
{
    if (fractional_count > fractional_mask) {
        throw std::invalid_argument("fractional count exceeds 24 bits");
    }
    uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        materialize(m_state, absolute_ns);
        m_state.anchor_count = count;
        m_state.fractional_count = fractional_count;
        generation = ++m_state.generation;
    }
    notify_observers(generation);
    return true;
}

bool arm_system_counter::write_count_part_at(uint32_t value, bool high,
                                             int64_t absolute_ns)
{
    uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        materialize(m_state, absolute_ns);
        if (high) {
            m_state.anchor_count = (uint64_t(value) << 32) |
                                   uint32_t(m_state.anchor_count);
        } else {
            m_state.anchor_count =
                (m_state.anchor_count & 0xffffffff00000000ULL) | value;
        }
        m_state.fractional_count = 0;
        generation = ++m_state.generation;
    }
    notify_observers(generation);
    return true;
}

bool arm_system_counter::write_count_low_at(uint32_t value,
                                            int64_t absolute_ns)
{
    return write_count_part_at(value, false, absolute_ns);
}

bool arm_system_counter::write_count_high_at(uint32_t value,
                                             int64_t absolute_ns)
{
    return write_count_part_at(value, true, absolute_ns);
}

void arm_system_counter::set_reported_frequency_hz(uint64_t frequency_hz)
{
    if (frequency_hz == 0) {
        throw std::invalid_argument("reported frequency must be nonzero");
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state.reported_frequency_hz = frequency_hz;
}

void arm_system_counter::freeze_mutations()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state.mutations_frozen = true;
}

void arm_system_counter::reset_at(int64_t absolute_ns)
{
    uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        validate_time(m_state, absolute_ns);
        const bool mutations_frozen = m_state.mutations_frozen;
        generation = m_state.generation + 1;
        m_state = m_reset_state;
        m_state.anchor_time_ns = absolute_ns;
        m_state.mutations_frozen = mutations_frozen;
        m_state.generation = generation;
    }
    notify_observers(generation);
}

void arm_system_counter::notify_observers(uint64_t generation) { m_observers->notify(generation); }

}

void module_register()
{
    typedef gs::arm_system_counter arm_system_counter;
    GSC_MODULE_REGISTER_C(arm_system_counter);
}
