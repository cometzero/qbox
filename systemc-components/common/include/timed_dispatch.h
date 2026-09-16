/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GS_TIMED_DISPATCH_H
#define GS_TIMED_DISPATCH_H

#include <systemc>

namespace gs {

/* GetTime returns an absolute request timestamp, not a relative delay.
 * The dispatcher must wait for the callback to complete.
 */
template <typename Dispatch, typename GetTime, typename Transport, typename SetTime>
void timed_dispatch(Dispatch dispatch, GetTime get_time, Transport transport, SetTime set_time)
{
    const sc_core::sc_time request = get_time();
    dispatch([&] {
        // Queueing cannot move an already issued request into the future.
        // In particular, a free-running CPU clock keeps advancing while its
        // native thread is blocked waiting for this callback.
        const sc_core::sc_time now = sc_core::sc_time_stamp();
        sc_core::sc_time delay = request > now ? request - now : sc_core::SC_ZERO_TIME;
        transport(delay);
        // Consume the returned offset before the caller resumes and kernel
        // time can advance again. Targets may wait or annotate a delay.
        set_time(delay);
    });
}

} // namespace gs

#endif
