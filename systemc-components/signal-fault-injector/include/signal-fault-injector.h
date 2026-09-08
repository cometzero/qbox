/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef GS_SIGNAL_FAULT_INJECTOR_H
#define GS_SIGNAL_FAULT_INJECTOR_H

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

#include <async_event.h>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <runonsysc.h>
#include <systemc>

namespace gs {

struct SignalFaultSnapshot {
    std::string action;
    bool source_level = false;
    bool output_level = false;
    bool drop_pending = false;
    bool pulse_active = false;
    uint64_t generation = 0;
    uint64_t match_count = 0;
};

class signal_fault_injector : public sc_core::sc_module
{
public:
    TargetSignalSocket<bool> signal_in;
    InitiatorSignalSocket<bool> signal_out;
    TargetSignalSocket<bool> reset;

    SC_HAS_PROCESS(signal_fault_injector);
    explicit signal_fault_injector(sc_core::sc_module_name name);

    bool arm(const std::string& action, uint64_t duration_ns = 0);
    bool clear();
    SignalFaultSnapshot snapshot() const;

private:
    enum class Mode { PASS, DROP_NEXT_ASSERT, FORCE_HIGH, FORCE_LOW, PULSE };
    enum class UpdateKind { SOURCE, RESET };

    struct PendingUpdate {
        UpdateKind kind = UpdateKind::SOURCE;
        bool level = false;
    };

    static constexpr std::size_t UPDATE_CAPACITY = 64;

    void enqueue_update(UpdateKind kind, bool level);
    void drain_updates();
    void expiry();
    void apply_source(bool level);
    void apply_reset(bool asserted);
    bool arm_internal(const std::string& action, uint64_t duration_ns);
    void clear_internal();
    static const char* mode_name(Mode mode);

    mutable std::mutex m_state_mutex;
    Mode m_mode = Mode::PASS;
    bool m_source_level = false;
    bool m_output_level = false;
    bool m_suppress_until_deassert = false;
    uint64_t m_generation = 0;
    uint64_t m_match_count = 0;

    std::mutex m_update_mutex;
    std::array<PendingUpdate, UPDATE_CAPACITY> m_updates;
    std::size_t m_update_head = 0;
    std::size_t m_update_count = 0;
    bool m_update_overflow = false;

    gs::async_event m_update_event;
    sc_core::sc_event m_expiry_event;
    gs::runonsysc m_run_on_sysc;
};

} // namespace gs

extern "C" void module_register();

#endif
