/* SPDX-License-Identifier: BSD-3-Clause */

#include <arm_system_counter.h>

#include <limits>
#include <stdexcept>

namespace gs {

namespace {

using Wide = unsigned __int128;

constexpr Wide ns_per_second = 1000000000;
constexpr Wide fixed_modulus = Wide(1) << (64 + arm_system_counter::fractional_bits);
constexpr Wide fixed_mask = fixed_modulus - 1;

Wide divide_ceil(Wide numerator, Wide denominator)
{
    return numerator / denominator + (numerator % denominator != 0);
}

Wide fixed_value(const arm_system_counter::StateSnapshot& state)
{
    return (Wide(state.anchor_count) << arm_system_counter::fractional_bits) |
           state.fractional_count;
}

Wide tick_product_modulo(Wide ticks, uint64_t increment)
{
    const uint64_t low_ticks = static_cast<uint64_t>(ticks);
    const Wide low_product = Wide(low_ticks) * increment;
    const Wide high_ticks = ticks >> 64;
    const Wide high_product_low =
        (high_ticks * increment) & arm_system_counter::fractional_mask;
    return (low_product + (high_product_low << 64)) & fixed_mask;
}

}

int64_t arm_system_counter::absolute_ns(const sc_core::sc_time& time)
{
    const sc_dt::uint64 ticks_per_ns =
        sc_core::sc_time(1, sc_core::SC_NS).value();
    if (ticks_per_ns == 0 || time.value() % ticks_per_ns != 0) {
        throw std::invalid_argument("SystemC time is not an exact nanosecond");
    }
    const sc_dt::uint64 value_ns = time.value() / ticks_per_ns;
    if (value_ns > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        throw std::out_of_range("SystemC time exceeds signed nanoseconds");
    }
    return static_cast<int64_t>(value_ns);
}

void arm_system_counter::validate_time(const StateSnapshot& state,
                                       int64_t absolute_ns)
{
    if (absolute_ns < 0 || absolute_ns < state.anchor_time_ns) {
        throw std::out_of_range("counter timestamp precedes the current anchor");
    }
}

void arm_system_counter::materialize(StateSnapshot& state, int64_t absolute_ns)
{
    validate_time(state, absolute_ns);
    const uint64_t elapsed_ns =
        static_cast<uint64_t>(absolute_ns - state.anchor_time_ns);
    const Wide input_numerator = Wide(elapsed_ns) * state.input_frequency_hz +
                                 state.input_tick_remainder;
    const Wide input_ticks = input_numerator / ns_per_second;
    state.input_tick_remainder =
        static_cast<uint64_t>(input_numerator % ns_per_second);

    Wide value = fixed_value(state);
    if (state.running() && state.increment_8_24 != 0) {
        value = (value + tick_product_modulo(input_ticks,
                                             state.increment_8_24)) & fixed_mask;
    }
    state.anchor_count = static_cast<uint64_t>(value >> fractional_bits);
    state.fractional_count = static_cast<uint32_t>(value & fractional_mask);
    state.anchor_time_ns = absolute_ns;
}

arm_system_counter::StateSnapshot arm_system_counter::snapshot_at(
    int64_t absolute_ns) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    StateSnapshot evaluated = m_state;
    materialize(evaluated, absolute_ns);
    return evaluated;
}

uint64_t arm_system_counter::count_at(const StateSnapshot& state,
                                      int64_t absolute_ns)
{
    StateSnapshot evaluated = state;
    materialize(evaluated, absolute_ns);
    return evaluated.anchor_count;
}

bool arm_system_counter::deadline_ns(const StateSnapshot& state,
                                     uint64_t target_count, int64_t from_ns,
                                     int64_t& deadline)
{
    StateSnapshot current = state;
    materialize(current, from_ns);
    if (!current.running() || current.increment_8_24 == 0) {
        return false;
    }
    const uint64_t count_delta = target_count - current.anchor_count;
    if (count_delta == 0 ||
        count_delta > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        deadline = from_ns;
        return true;
    }

    const Wide delta = (Wide(count_delta) << fractional_bits) -
                       current.fractional_count;
    const Wide ticks_needed = divide_ceil(delta, current.increment_8_24);
    const Wide time_numerator = ticks_needed * ns_per_second -
                                current.input_tick_remainder;
    const Wide elapsed_ns = divide_ceil(time_numerator,
                                        current.input_frequency_hz);
    if (elapsed_ns > static_cast<Wide>(std::numeric_limits<int64_t>::max() -
                                      from_ns)) {
        return false;
    }

    const int64_t candidate = from_ns + static_cast<int64_t>(elapsed_ns);
    StateSnapshot at_candidate = current;
    materialize(at_candidate, candidate);
    const uint64_t remaining = target_count - at_candidate.anchor_count;
    if (remaining != 0 &&
        remaining <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return false;
    }
    deadline = candidate;
    return true;
}

}
