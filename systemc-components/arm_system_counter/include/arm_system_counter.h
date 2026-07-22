/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include <cci_configuration>
#include <systemc>

#include <module_factory_registery.h>

namespace gs {

class arm_system_counter : public sc_core::sc_module
{
private:
    struct ObserverRegistry;

public:
    static constexpr uint32_t fractional_bits = 24;
    static constexpr uint32_t fractional_mask = (1u << fractional_bits) - 1;

    struct StateSnapshot {
        uint64_t anchor_count = 0;
        int64_t anchor_time_ns = 0;
        uint64_t input_tick_remainder = 0;
        uint32_t fractional_count = 0;
        uint64_t input_frequency_hz = 125000000;
        uint64_t increment_8_24 = uint64_t(1) << fractional_bits;
        bool enabled = true;
        bool halt_on_debug = false;
        bool debug_halted = false;
        bool mutations_frozen = false;
        uint64_t reported_frequency_hz = 125000000;
        uint64_t generation = 0;

        uint64_t integer_increment() const { return increment_8_24 >> fractional_bits; }
        bool running() const { return enabled && !(halt_on_debug && debug_halted); }
    };

    class ObserverSubscription
    {
    private:
        std::weak_ptr<ObserverRegistry> m_registry;
        uint64_t m_id = 0;

        ObserverSubscription(const std::shared_ptr<ObserverRegistry>& registry,
                             uint64_t id);
        friend class arm_system_counter;

    public:
        ObserverSubscription() = default;
        ~ObserverSubscription();
        ObserverSubscription(const ObserverSubscription&) = delete;
        ObserverSubscription& operator=(const ObserverSubscription&) = delete;
        ObserverSubscription(ObserverSubscription&& other) noexcept;
        ObserverSubscription& operator=(ObserverSubscription&& other) noexcept;

        void reset();
        explicit operator bool() const { return m_id != 0; }
    };

private:
    cci::cci_param<uint64_t> p_input_frequency_hz;
    cci::cci_param<uint64_t> p_integer_increment;
    cci::cci_param<uint64_t> p_reported_frequency_hz;
    cci::cci_param<bool> p_enabled;
    cci::cci_param<bool> p_halt_on_debug;
    cci::cci_param<uint64_t> p_initial_count;

    mutable std::mutex m_mutex;
    StateSnapshot m_state;
    StateSnapshot m_reset_state;
    std::shared_ptr<ObserverRegistry> m_observers;

    static void validate_time(const StateSnapshot& state, int64_t absolute_ns);
    static void materialize(StateSnapshot& state, int64_t absolute_ns);
    static uint64_t count_at(const StateSnapshot& state, int64_t absolute_ns);
    static bool deadline_ns(const StateSnapshot& state, uint64_t target_count,
                            int64_t from_ns, int64_t& deadline_ns);
    static void require_mutable(const StateSnapshot& state);
    bool mutate_at(int64_t absolute_ns,
                   const std::function<bool(StateSnapshot&)>& mutation,
                   bool structural);
    bool write_count_part_at(uint32_t value, bool high, int64_t absolute_ns);
    void notify_observers(uint64_t generation);

public:
    explicit arm_system_counter(const sc_core::sc_module_name& name);
    ~arm_system_counter() override;

    static int64_t absolute_ns(const sc_core::sc_time& time);

    StateSnapshot snapshot() const;
    StateSnapshot snapshot_at(int64_t absolute_ns) const;
    uint64_t count_at(int64_t absolute_ns) const;
    uint64_t count_at(const sc_core::sc_time& time) const;
    bool deadline_ns(uint64_t target_count, int64_t from_ns,
                     int64_t& deadline_ns) const;

    ObserverSubscription observe(std::function<void(uint64_t)> observer);

    bool set_input_frequency_at(uint64_t frequency_hz, int64_t absolute_ns);
    bool set_integer_increment_at(uint64_t increment, int64_t absolute_ns);
    bool set_scale_8_24_at(uint32_t scale, int64_t absolute_ns);
    bool set_control_at(bool enabled, bool halt_on_debug,
                        int64_t absolute_ns);
    bool set_enabled_at(bool enabled, int64_t absolute_ns);
    bool set_halt_on_debug_at(bool halt_on_debug, int64_t absolute_ns);
    bool set_debug_halted_at(bool debug_halted, int64_t absolute_ns);
    bool reanchor_at(uint64_t count, uint32_t fractional_count,
                     int64_t absolute_ns);
    bool write_count_low_at(uint32_t value, int64_t absolute_ns);
    bool write_count_high_at(uint32_t value, int64_t absolute_ns);
    void set_reported_frequency_hz(uint64_t frequency_hz);
    void freeze_mutations();
    void reset_at(int64_t absolute_ns);
};

}

extern "C" void module_register();
