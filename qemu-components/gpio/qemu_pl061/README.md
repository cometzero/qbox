# QEMU PL061 wrapper

`qemu_pl061` exposes QEMU's `pl061` QOM device as a reusable QBox module.
It provides one 4 KiB MMIO socket, one SysBus interrupt output, and eight
input and output GPIO signal sockets.

The `init_inputs`, `pullups`, and `pulldowns` CCI parameters are eight-bit
pin masks and default to zero to match the FVP GPIO integration. `pullups`
and `pulldowns` must not overlap. Bind `reset` to the same reset source as the
logic that cold-resets the wrapped QEMU device; the wrapper re-applies the
current input levels after reset deassertion.
