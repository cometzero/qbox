/* SPDX-License-Identifier: BSD-3-Clause */

#include <arm_system_counter.h>

namespace gs {

bool arm_system_counter::set_control_at(bool enabled, bool halt_on_debug,
                                        int64_t absolute_ns)
{
    return mutate_at(absolute_ns,
                     [enabled, halt_on_debug](StateSnapshot& state) {
        if (state.enabled == enabled &&
            state.halt_on_debug == halt_on_debug) {
            return false;
        }
        state.enabled = enabled;
        state.halt_on_debug = halt_on_debug;
        return true;
    }, false);
}

bool arm_system_counter::set_enabled_at(bool enabled, int64_t absolute_ns)
{
    return mutate_at(absolute_ns, [enabled](StateSnapshot& state) {
        if (state.enabled == enabled) {
            return false;
        }
        state.enabled = enabled;
        return true;
    }, false);
}

bool arm_system_counter::set_halt_on_debug_at(bool halt_on_debug,
                                              int64_t absolute_ns)
{
    return mutate_at(absolute_ns, [halt_on_debug](StateSnapshot& state) {
        if (state.halt_on_debug == halt_on_debug) {
            return false;
        }
        state.halt_on_debug = halt_on_debug;
        return true;
    }, false);
}

bool arm_system_counter::set_debug_halted_at(bool debug_halted,
                                             int64_t absolute_ns)
{
    return mutate_at(absolute_ns, [debug_halted](StateSnapshot& state) {
        if (state.debug_halted == debug_halted) {
            return false;
        }
        state.debug_halted = debug_halted;
        return true;
    }, false);
}

}
