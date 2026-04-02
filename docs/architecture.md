# PicoMSO Firmware Architecture

This document describes the current firmware layout under `firmware/`.

## Top-level structure

| Path | Role |
|---|---|
| `firmware/app/` | Pico SDK application entry point |
| `firmware/common/` | Shared runtime utilities and controller state |
| `firmware/mixed_signal/` | Logic and scope capture backends |
| `firmware/protocol/` | Packet definitions and command dispatch |
| `firmware/transport/` | Transport abstraction and USB transport backend |
| `firmware/integration/` | Transport-to-protocol glue |

## Runtime flow

The current firmware path is:

```text
firmware/app
  -> firmware/transport/usb
  -> firmware/integration
  -> firmware/protocol
  -> firmware/common
  -> firmware/mixed_signal
```

`firmware/app/main.c` initializes the USB transport backend, binds it to the
generic transport context, binds that context to the integration layer, and then
polls `integration_process_one()` in the main loop.

## Common layer

`firmware/common/` contains:

- debug logging support
- shared type definitions
- `capture_controller_t`

`capture_controller_t` tracks:

- `streams_enabled`
- `state`

The stream bitmask values are:

- `PICOMSO_STREAM_NONE`
- `PICOMSO_STREAM_LOGIC`
- `PICOMSO_STREAM_SCOPE`

Both logic-only, scope-only, and combined logic+scope selections are supported
by the current controller and protocol code.

The common controller represents the externally visible capture state used by the
protocol layer, while each capture backend keeps its own internal runtime state.

## Mixed-signal capture layer

`firmware/mixed_signal/` provides two concrete backends:

- `logic_capture.*`
- `scope_capture.*`

Both backends implement the same high-level lifecycle:

1. accept a `REQUEST_CAPTURE`
2. perform a finite capture
3. store finalized data
4. serve fixed-size blocks through `READ_DATA_BLOCK`

Captures are finite acquisitions, not continuous streaming sessions.

Each backend owns its own capture progress, finalized sample storage, and read
offset state used when serving host block-read requests.

In mixed-signal mode, logic and scope data are handled as two independent data
streams. They are not interleaved into a single combined payload.

## Protocol layer

`firmware/protocol/` defines the PicoMSO packet header, request and response
payloads, and the dispatch logic for:

- `GET_INFO`
- `GET_CAPABILITIES`
- `GET_STATUS`
- `SET_MODE`
- `REQUEST_CAPTURE`
- `READ_DATA_BLOCK`

`SET_MODE` currently takes a stream bitmask payload. The command name is kept as
implemented in the wire protocol, but the behavior is stream selection rather
than selection of a separate standalone firmware image.

`REQUEST_CAPTURE` starts a finite acquisition for the currently enabled stream
selection.

`READ_DATA_BLOCK` is a host-driven pull mechanism. Each request returns the next
available finalized block from one enabled stream.

`READ_DATA_BLOCK` returns `PICOMSO_MSG_DATA_BLOCK` responses that include a
`stream_id`, so the host can distinguish logic and scope blocks when both
streams are enabled.

## Transport and integration

`firmware/transport/` is a transport abstraction layer with:

- `transport_ctx_t`
- `transport_interface_t`
- helpers for send, receive, and readiness checks

`firmware/transport/usb/` provides the RP2040 USB backend used by
`firmware/app/`.

`firmware/integration/` is the thin receive/dispatch/send loop that connects a
transport backend to `picomso_dispatch()`. It also includes the dummy transport
used for integration-style validation without hardware.

## Build entry point

Build the current firmware application from `firmware/app/`. See
[`docs/building.md`](building.md) for commands.