/* SPDX-License-Identifier: BSD-3-Clause */

#include <signal-fault-injector.h>

namespace gs {

signal_fault_injector::signal_fault_injector(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , signal_in("signal_in")
    , signal_out("signal_out")
    , reset("reset")
    , m_update_event(false)
    , m_run_on_sysc("run_on_sysc")
{
    signal_in.register_value_changed_cb([this](bool level) { enqueue_update(UpdateKind::SOURCE, level); });
    reset.register_value_changed_cb([this](bool asserted) { enqueue_update(UpdateKind::RESET, asserted); });

    SC_METHOD(drain_updates);
    sensitive << m_update_event;
    dont_initialize();

    SC_METHOD(expiry);
    sensitive << m_expiry_event;
    dont_initialize();
}

void signal_fault_injector::enqueue_update(UpdateKind kind, bool level)
{
    {
        std::lock_guard<std::mutex> lock(m_update_mutex);
        if (m_update_count == UPDATE_CAPACITY) {
            m_update_head = (m_update_head + 1) % UPDATE_CAPACITY;
            --m_update_count;
            m_update_overflow = true;
        }
        const std::size_t tail = (m_update_head + m_update_count) % UPDATE_CAPACITY;
        m_updates[tail] = { kind, level };
        ++m_update_count;
    }
    m_update_event.notify();
}

void signal_fault_injector::drain_updates()
{
    bool overflow = false;
    for (;;) {
        PendingUpdate update;
        {
            std::lock_guard<std::mutex> lock(m_update_mutex);
            if (m_update_count == 0) {
                overflow = m_update_overflow;
                m_update_overflow = false;
                break;
            }
            update = m_updates[m_update_head];
            m_update_head = (m_update_head + 1) % UPDATE_CAPACITY;
            --m_update_count;
        }

        if (update.kind == UpdateKind::SOURCE) {
            apply_source(update.level);
        } else {
            apply_reset(update.level);
        }
    }

    if (overflow) {
        SC_REPORT_WARNING(name(), "signal update queue overflowed; oldest updates were discarded");
    }
}

void signal_fault_injector::apply_source(bool level)
{
    bool output;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        const bool rising = !m_source_level && level;
        m_source_level = level;

        if (m_suppress_until_deassert) {
            if (!level) m_suppress_until_deassert = false;
            output = false;
        } else if (m_mode == Mode::DROP_NEXT_ASSERT && rising) {
            m_mode = Mode::PASS;
            m_suppress_until_deassert = true;
            ++m_match_count;
            output = false;
        } else if (m_mode == Mode::FORCE_HIGH || m_mode == Mode::PULSE) {
            output = true;
        } else if (m_mode == Mode::FORCE_LOW) {
            output = false;
        } else {
            output = level;
        }
        m_output_level = output;
    }
    if (signal_out.size()) signal_out->write(output);
}

void signal_fault_injector::apply_reset(bool asserted)
{
    if (asserted) clear_internal();
}

bool signal_fault_injector::arm(const std::string& action, uint64_t duration_ns)
{
    if (m_run_on_sysc.is_on_sysc()) return arm_internal(action, duration_ns);

    bool accepted = false;
    const bool ran = m_run_on_sysc.run_on_sysc(
        [this, &accepted, action, duration_ns]() { accepted = arm_internal(action, duration_ns); });
    return ran && accepted;
}

bool signal_fault_injector::arm_internal(const std::string& action, uint64_t duration_ns)
{
    Mode next;
    if (action == "pass") {
        if (duration_ns != 0) return false;
        clear_internal();
        return true;
    } else if (action == "drop-next-assert") {
        next = Mode::DROP_NEXT_ASSERT;
    } else if (action == "force-high") {
        next = Mode::FORCE_HIGH;
    } else if (action == "force-low") {
        next = Mode::FORCE_LOW;
    } else if (action == "pulse") {
        if (duration_ns == 0) return false;
        next = Mode::PULSE;
    } else {
        return false;
    }

    m_expiry_event.cancel();
    bool output;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_mode = next;
        m_suppress_until_deassert = false;
        ++m_generation;
        if (next == Mode::FORCE_HIGH || next == Mode::PULSE) {
            output = true;
        } else if (next == Mode::FORCE_LOW) {
            output = false;
        } else {
            output = m_source_level;
        }
        m_output_level = output;
    }
    if (signal_out.size()) signal_out->write(output);

    if (duration_ns != 0) {
        m_expiry_event.notify(sc_core::sc_time(duration_ns, sc_core::SC_NS));
    }
    return true;
}

bool signal_fault_injector::clear()
{
    if (m_run_on_sysc.is_on_sysc()) {
        clear_internal();
        return true;
    }
    return m_run_on_sysc.run_on_sysc([this]() { clear_internal(); });
}

void signal_fault_injector::clear_internal()
{
    m_expiry_event.cancel();
    bool output;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_mode = Mode::PASS;
        m_suppress_until_deassert = false;
        ++m_generation;
        output = m_source_level;
        m_output_level = output;
    }
    if (signal_out.size()) signal_out->write(output);
}

void signal_fault_injector::expiry() { clear_internal(); }

SignalFaultSnapshot signal_fault_injector::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    SignalFaultSnapshot state;
    state.action = mode_name(m_mode);
    state.source_level = m_source_level;
    state.output_level = m_output_level;
    state.drop_pending = m_mode == Mode::DROP_NEXT_ASSERT;
    state.pulse_active = m_mode == Mode::PULSE;
    state.generation = m_generation;
    state.match_count = m_match_count;
    return state;
}

const char* signal_fault_injector::mode_name(Mode mode)
{
    switch (mode) {
    case Mode::DROP_NEXT_ASSERT:
        return "drop-next-assert";
    case Mode::FORCE_HIGH:
        return "force-high";
    case Mode::FORCE_LOW:
        return "force-low";
    case Mode::PULSE:
        return "pulse";
    case Mode::PASS:
    default:
        return "pass";
    }
}

} // namespace gs

typedef gs::signal_fault_injector signal_fault_injector;

void module_register() { GSC_MODULE_REGISTER_C(signal_fault_injector); }
