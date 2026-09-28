# QBox Monitor

The QBox monitor is a web-based monitoring interface that
exposes the state of a running simulation through a set of
REST endpoints and WebSocket connections. It uses the Crow
web framework internally to serve an HTML dashboard and to
stream data in real time.

## Features

- Query the current simulation time (`/sc_time`).
- Pause and resume the simulation (`/pause`, `/continue`).
- Browse the SystemC object hierarchy and inspect CCI
  parameters (`/object/`, `/object/<name>`).
- View quantum-keeper status for multi-threaded simulations
  (`/qk_status`).
- Read memory through the TLM debug transport interface
  (`/transport_dbg/<addr>/<name>`).
- Connect to any `biflow_socket` in the design via WebSocket
  (`/biflow/<name>`), enabling browser-based VNC or serial
  console sessions.

During elaboration the monitor automatically discovers every
`biflow_multibindable` socket in the design and makes it
available over WebSocket.

## CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `server_port` | `uint32_t` | `18080` | HTTP port the monitor listens on |
| `bind_address` | `string` | `"127.0.0.1"` | HTTP listener address |
| `runtime_mutation` | `bool` | `false` | Enable runtime action endpoints |
| `injection_service` | `string` | `""` | Exact SystemC object path implementing `gs::RuntimeActionService` |
| `html_doc_template_dir_path` | `string` | (executable-relative `static/`) | Directory containing HTML templates |
| `html_doc_name` | `string` | `"monitor.html"` | Name of the main HTML document |
| `use_html_presentation` | `bool` | `true` | Serve the HTML dashboard; when false, the root URL returns a plain-text API listing |

## Example Configuration

```lua
platform["monitor_0"] = {
    moduletype = "monitor",
    bind_address = "127.0.0.1",
    server_port = 18080,
    use_html_presentation = true,
    html_doc_template_dir_path = "/path/to/html/templates",
    html_doc_name = "monitor.html",
}
```

Runtime mutation is disabled unless `runtime_mutation` is explicitly set.
When enabled, `bind_address` must be `127.0.0.1` or `::1`; any other address
causes construction to fail before the listener starts. Read-only monitor
routes remain available when mutation is disabled.

The runtime action service is a platform-owned SystemC object that publicly
implements the typed, platform-independent contract in
`runtime-action-service.h`. The monitor resolves `injection_service` by its
exact SystemC object path and invokes it only through `gs::runonsysc`.

## Metadata and dashboard sampling

`GET /api/v1/objects` returns
`{"schema_version":1,"objects":[{"name":"platform","basename":"platform","kind":"sc_module"}]}`.
`GET /api/v1/objects/<name>` returns `schema_version`, `object` (the same
three fields), and `children` (direct children only). These endpoints run on
SystemC and perform no TLM transactions, router probes, or CCI value callbacks.
Missing objects return `404 object-not-found`; stopped SystemC execution
returns `503 simulation-unavailable`. The legacy `/object/` endpoints retain
their socket-probing behavior and must not be used for automatic polling.

`/transport_dbg/<addr>/<name>` uses an unsigned decimal byte address and reads
one aligned 32-bit word on SystemC. Unaligned addresses return 400, missing
target sockets 404, and short or explicit TLM-error responses 502. A target
may leave TLM status INCOMPLETE when returning all four bytes, as allowed by
the debug transport byte-count contract. This endpoint is not an MMIO safety
allowlist; dashboard clients must restrict accessible targets separately.

`/qk_status` now samples on SystemC and uses atomically published timing and
wait flags. `local_time` is absolute QK simulation time; `quantum_time` is its
nonnegative offset from SystemC time. Fields are approximate observations,
not an atomic machine snapshot or Guest CPU utilization. MCIPS snapshots take
the producer mutex for shared timing state, while QEMU instruction counters
remain live approximate values.

`runonsysc` does not impose a wall-clock deadline. Its shutdown hook cancels
pending jobs, but HTTP clients must use bounded timeouts; a client timeout
does not cancel an already submitted action. `/sc_suspended` reflects kernel
scheduling suspension, which may also occur during normal quantum keeper
synchronization. It must not alone be interpreted as a user Pause state.
Its separate `monitor_paused` boolean records the last successfully executed
monitor pause/continue request and is independent of transient QK suspension.
Full-system Pause/Resume remains unqualified until platform traffic and
repeated resume/reset tests pass.

When QemuInstances exist, monitor control acquires one shared global pause
worker during elaboration. It reuses the debugger worker's sequencing without
installing debugger VM-state callbacks: stop all running QEMU instances in
parallel while SystemC can still service MMIO, assert CPU synchronization
holds, then suspend SystemC. Resume releases SystemC first, restarts only
instances stopped by this transition, then releases holds. A debugger-owned
or second monitor coordinator is rejected (409). The completion wait is
bounded to 1500 ms; a 503 `control-unknown` leaves the pending operation intact
and must not trigger automatic replay. `/sc_suspended.monitor_paused` reflects
completed coordinated state. CPU-free component fixtures retain SystemC-only
pause. Full-system qualification must verify CPU local times also remain fixed;
SystemC time alone cannot detect a running freerunning QK CPU.

## Runtime Action API

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/api/v1/injection/capabilities` | List allow-listed targets and actions |
| `GET` | `/api/v1/injection/targets/<target>` | Read a target snapshot |
| `POST` | `/api/v1/injections` | Submit a typed runtime action |
| `GET` | `/api/v1/injections` | List retained requests |
| `GET` | `/api/v1/injections/<id>` | Read request status |
| `DELETE` | `/api/v1/injections/<id>` | Cancel a request |

Requests use schema version 1. `trigger` defaults to `immediate`; the other
supported form is `relative-simulation-time` with an unsigned `delay_ns`.
Parameter values are limited to booleans, unsigned integers, and strings.

```json
{
  "schema_version": 1,
  "target": "platform.runtime_target",
  "action": "trigger",
  "trigger": {
    "type": "relative-simulation-time",
    "delay_ns": 10000
  },
  "parameters": {
    "duration_ns": 5000
  },
  "clear_on_reset": true
}
```

An accepted request returns HTTP `202` with its typed status. Invalid JSON or
unsupported value types return `400 invalid-request`; a disabled mutation API
returns `403 mutation-disabled`; a missing service or stopped SystemC bridge
returns `503 simulation-unavailable`. Error responses use
`{"error":{"code":"...","message":"..."}}`.

After the simulation starts, open `http://localhost:18080/` in
a browser to access the dashboard.

The SystemC status, quantum-keeper tables, REST data, and object
browser render without external web resources. The enhanced xterm
presentation loads asynchronously from jsDelivr; if the CDN is
unavailable, the dashboard remains usable and shows a basic terminal
fallback instead of blocking on a blank page.
