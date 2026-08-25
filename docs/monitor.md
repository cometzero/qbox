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
