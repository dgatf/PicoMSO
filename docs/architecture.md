# PicoMSO Firmware Architecture

This document describes the current shared-code structure of the PicoMSO firmware,
what has been extracted into `firmware/common/`, what remains duplicated and why,
and what is unsafe to unify at this stage.

---

## What Was Extracted

### `firmware/common/include/debug.h` + `firmware/common/src/debug.c`

Both `logic_analyzer_rp2040` and `oscilloscope_rp2040` contained nearly identical
debug-logging helpers built on `uart0` (GPIO 16 TX / GPIO 17 RX). The only
meaningful differences were:

- The logic-analyzer's `debug_reinit()` hardcoded baud rate 115200 instead of
  re-using the value passed to `debug_init()`.
- The oscilloscope called `sleep_ms(1000)` inside `debug_init()` to give a USB
  host time to connect before emitting the first message.

The unified implementation uses the oscilloscope approach of storing the baud rate
so `debug_reinit()` does not need to duplicate it. The `sleep_ms(1000)` was moved
to the oscilloscope's `main.c` (right after the `debug_init()` call) so that
observable startup behaviour is unchanged while keeping the shared library free of
device-specific timing.

The shared header also names the UART pin constants explicitly:

| Constant            | Value | Purpose                                      |
|---------------------|-------|----------------------------------------------|
| `DEBUG_UART_TX_GPIO`| 16    | UART0 TX – debug serial output               |
| `DEBUG_UART_RX_GPIO`| 17    | UART0 RX – reserved (not currently used)     |
| `DEBUG_ENABLE_GPIO` | 18    | Pull low at boot to enable debug output      |

### `firmware/common/include/types.h`

Shared type definitions extracted into a single header:

| Type                | Origin          | Notes                                         |
|---------------------|-----------------|-----------------------------------------------|
| `capture_state_t`   | new (common)    | `CAPTURE_IDLE`, `CAPTURE_RUNNING`             |
| `trigger_match_t`   | logic analyzer  | Level/edge trigger match kinds                |
| `trigger_t`         | logic analyzer  | Single-channel trigger configuration          |
| `capture_config_t`  | logic analyzer  | Full LA capture configuration struct          |

`capture_state_t` replaces the oscilloscope-local `state_t { IDLE, RUNNING }`.
All four files that referenced `IDLE` / `RUNNING` have been updated to use
`CAPTURE_IDLE` / `CAPTURE_RUNNING`. The numeric values are identical (0 and 1),
so behaviour is unchanged.

`capture_config_t`, `trigger_t`, and `trigger_match_t` were unique to the logic
analyzer. Moving them to `firmware/common/` makes them available to future shared
code (e.g. a mixed-signal capture path) without requiring further changes to the
logic-analyzer sources.

---

## Shared Capture Controller (`firmware/common/capture_controller`)

### Role

`capture_controller_t` is a minimal, backend-agnostic value object that records
*which* capture subsystem is active and *what state* it is currently in.  It
provides a single canonical place for the two shared concepts that are safe to
express at the common layer:

| Field   | Type              | Meaning                                          |
|---------|-------------------|--------------------------------------------------|
| `mode`  | `capture_mode_t`  | Which backend is selected (logic / oscilloscope) |
| `state` | `capture_state_t` | Whether a capture is idle or running             |

### What It Owns

- `capture_mode_t` — `CAPTURE_MODE_UNSET`, `CAPTURE_MODE_LOGIC`,
  `CAPTURE_MODE_OSCILLOSCOPE`
- `capture_state_t` — `CAPTURE_IDLE`, `CAPTURE_RUNNING` (already in `types.h`)
- `capture_controller_t` — struct holding exactly those two fields
- Five inline-equivalent helper functions: `capture_controller_init`,
  `capture_controller_set_mode`, `capture_controller_set_state`,
  `capture_controller_get_mode`, `capture_controller_get_state`

### What Remains Backend-Specific

The controller deliberately does **not** manage:

| Concern                  | Location                          |
|--------------------------|-----------------------------------|
| Hardware init (ADC, PIO) | `oscilloscope.c` / `capture.c`    |
| DMA channel config       | `oscilloscope.c` / `capture.c`    |
| Trigger execution        | `capture.c` (PIO-based)           |
| Sample buffers           | `oscilloscope.c` / `capture.c`    |
| USB / protocol handling  | `protocol.c`, `protocol_sump.c`   |
| Clock management         | each `main.c`                     |

### Why Intentionally Minimal

Both firmware projects already have working, independently tested capture
pipelines.  Introducing shared state management for concerns such as DMA,
ADC, or USB framing would require behaviour-changing rewrites with significant
risk and no near-term benefit.  The controller exists solely to give future
mixed-signal code (and tests) a stable, hardware-free handle on the shared
concepts of *mode* and *state* without touching any running code paths.

---

## What Remains Duplicated and Why

### `config_t` (project-specific runtime configuration)

Both projects have a `config_t` struct but the fields are entirely different:

- Logic analyzer: `{ uint channels; bool trigger_edge; bool debug; }`
- Oscilloscope:   `{ bool debug_is_enabled; bool no_conversion; bool is_multicore; }`

The only shared concept is "debug enabled", but the field names differ and the
surrounding logic is device-specific. Merging would require renaming fields in
both projects and is not worth the churn at this stage.

### Boot-time GPIO configuration (`set_pin_config`)

Both projects read GPIO 18 (debug enable) and GPIO 19 (mode select) with pull-ups
at boot. However:

- The logic analyzer reads **GPIO 19** to choose between stage-based and
  edge-based triggers (`GPIO_TRIGGER_STAGES`).
- The oscilloscope reads **GPIO 19** to choose 8-bit vs 12-bit ADC output
  (`GPIO_NO_CONVERSION`).

The pin number is the same but the semantic is completely different. A shared
helper would need to abstract away the meaning, which adds more complexity than
it removes. Left in each project's `main.c`.

### Protocol handlers

- Logic analyzer: `protocol_sump.c` implements the SUMP serial protocol over USB
  CDC (virtual COM port).
- Oscilloscope: `protocol.c` + `usb.c` implement a custom binary protocol over
  USB bulk transfers.

These are fundamentally different transports with different framing, command sets,
and timing requirements. They must not be merged.

### Capture backends

- Logic analyzer: `capture.c` uses PIO state machines and DMA for high-speed
  parallel digital sampling.
- Oscilloscope: `oscilloscope.c` uses the RP2040 ADC and DMA for analogue sampling.

Different peripherals, different DMA configurations, different buffer strategies.
Unsafe to unify.

### Debug buffer size

- Logic analyzer: `DEBUG_BUFFER_SIZE = 300`
- Oscilloscope:   `DEBUG_BUFFER_SIZE = 256`

Each project defines this in its own `common.h`. A shared constant would force
one project to grow or shrink its buffer for no functional benefit. Left as-is.

---

## What Is Unsafe to Unify Yet

| Area                      | Reason                                                          |
|---------------------------|-----------------------------------------------------------------|
| DMA channel assignments   | Each project uses a fixed, hardware-specific channel layout     |
| PIO programs              | Logic analyzer only; no oscilloscope equivalent                 |
| ADC configuration         | Oscilloscope only; no logic-analyzer equivalent                 |
| Clock management          | Different target frequencies (200 MHz vs 240 MHz); device-specific thresholds |
| USB transport             | SUMP over CDC vs custom bulk protocol                           |
| Trigger logic             | LA has 4-stage HW triggers via PIO; oscilloscope has none       |
| Pre-trigger ring buffer   | LA-specific; not applicable to streaming ADC                    |
| Multicore scheduling      | Oscilloscope uses core1 for protocol; LA is single-core         |

---

## Protocol Layer (`firmware/protocol/`)

### Role

`firmware/protocol/` contains the transport-agnostic skeleton of the future
unified PicoMSO host protocol.  It defines:

- The wire-format packet header (`picomso_packet_header_t`).
- The message-type enumeration (`picomso_msg_type_t`).
- Per-command request/response packet structures (`protocol_packets.h`).
- A dispatch entry point (`picomso_dispatch`) that validates an incoming
  byte buffer and routes it to the appropriate per-command handler.
- Helper functions to build ACK and ERROR response packets.

Phase 0 handles four commands: `GET_INFO`, `GET_CAPABILITIES`,
`GET_STATUS`, and `SET_MODE`.  `GET_STATUS` reads live mode and state from
`capture_controller_t`; `SET_MODE` writes to it.  `GET_INFO` and
`GET_CAPABILITIES` return static values.

### Relationship to Transport

The protocol layer has **no dependency** on:

| Concern               | Location (not in firmware/protocol)      |
|-----------------------|------------------------------------------|
| USB (CDC / bulk)      | Each project's `usb.c`, `protocol.c`     |
| UART / SPI            | Future transport adapters                |
| PIO, ADC, DMA         | Each project's capture back-end          |

Callers supply raw byte buffers and receive raw byte buffers in return.
Wiring those buffers to an actual transport is the responsibility of a
future transport adapter layer, which will live outside `firmware/protocol/`.

### Relationship to Capture Controller

`firmware/protocol/` now links against `firmware/common/` and uses
`capture_controller_t` as its control-plane state store.

A static `capture_controller_t` instance lives inside
`firmware/protocol/src/protocol_dispatch.c`.  The protocol layer is
the sole owner of this instance; no hardware back-end reads or writes it.

| Protocol command | capture_controller operation                  |
|------------------|-----------------------------------------------|
| `GET_STATUS`     | Reads mode via `capture_controller_get_mode`; reads state via `capture_controller_get_state` |
| `SET_MODE`       | Writes mode via `capture_controller_set_mode` |
| `GET_INFO`       | No capture_controller interaction (static)    |
| `GET_CAPABILITIES` | No capture_controller interaction (static) |

The protocol layer owns **control-plane state only**:

- It can change the selected mode (`LOGIC`, `OSCILLOSCOPE`, `UNSET`).
- It reflects the current capture state (`IDLE` / `RUNNING`).
- It does **not** start or stop captures, configure ADC/PIO/DMA, or touch
  any hardware peripheral.

The capture state (`RUNNING` / `IDLE`) is read-only from the protocol
layer's perspective.  A future transport adapter or higher-level controller
would call `capture_controller_set_state` on the same instance to signal
that a capture has started or stopped.

### What Remains Unchanged

- `logic_analyzer_rp2040` continues to use its SUMP-over-CDC protocol.
- `oscilloscope_rp2040` continues to use its custom USB bulk protocol.
- Neither project includes or links `firmware/protocol/`.
- No capture pipeline, DMA setup, or USB framing has been modified.

---

## Transport Layer (`firmware/transport/`)

### Role

`firmware/transport/` defines the minimal, generic interface through which
the protocol layer (and any future higher-level code) sends and receives raw
byte buffers.  It is intentionally transport-agnostic:

- **No USB dependency** – no TinyUSB headers, no CDC, no bulk endpoints.
- **No UART / SPI dependency** – no hardware peripheral assumptions.
- **No ADC / PIO / DMA dependency** – purely software abstraction.

### Key Types

| Type                    | Description                                                  |
|-------------------------|--------------------------------------------------------------|
| `transport_result_t`    | Enum of operation outcomes (`TRANSPORT_OK`, error codes)     |
| `transport_interface_t` | Function-pointer table: `is_ready`, `send`, `receive`        |
| `transport_ctx_t`       | Runtime context: pointer to interface + opaque `user_data`   |

### Helper API

| Function              | Description                                                   |
|-----------------------|---------------------------------------------------------------|
| `transport_init()`    | Bind an interface + user-data to a context; no hardware I/O  |
| `transport_is_ready()`| Delegate to `iface->is_ready`; treats NULL as always ready   |
| `transport_send()`    | Forward a byte buffer to `iface->send`                       |
| `transport_receive()` | Forward a buffer + length to `iface->receive`                |

### Relationship to Protocol

The protocol layer (`firmware/protocol/`) operates entirely on raw byte
buffers returned by `picomso_dispatch()`.  The integration layer
(`firmware/integration/`) performs the coupling:

1. Call `transport_receive()` to read an incoming packet from the wire.
2. Pass the buffer to `picomso_dispatch()`.
3. Call `transport_send()` to write the response back to the wire.

The protocol layer itself never calls into `firmware/transport/` directly;
that coupling lives exclusively in `firmware/integration/`.

### Phase 0 Backend: Dummy/Mock Only

Phase 0 ships the abstraction plus one concrete backend: the dummy/mock
transport defined in `firmware/integration/include/dummy_transport.h`.
It uses two in-memory byte arrays to simulate send and receive without any
hardware or OS dependency.  No USB CDC adapter, no USB bulk adapter, and
no UART adapter exist yet.

---

## Integration Layer (`firmware/integration/`)

### Role

`firmware/integration/` is the thin glue between the transport abstraction
and the protocol dispatch layer.  It owns:

- **`integration_ctx_t`** – a small context holding a pointer to the
  caller-supplied `transport_ctx_t`.
- **`integration_init()`** – binds a transport context to an integration
  context; performs no I/O.
- **`integration_process_one()`** – executes one full receive → dispatch →
  send cycle.
- **`dummy_transport_state_t`** + **`dummy_transport_iface`** – the
  Phase 0 in-memory mock backend.

### Dependencies

| Module               | Used for                                               |
|----------------------|--------------------------------------------------------|
| `picomso_transport`  | `transport_ctx_t`, `transport_receive`, `transport_send` |
| `picomso_protocol`   | `picomso_dispatch`, `picomso_response_t`               |

The integration layer has **no** dependency on:

| Concern              | Not present in `firmware/integration/`              |
|----------------------|-----------------------------------------------------|
| ADC / PIO / DMA      | No hardware peripherals                             |
| USB / UART / SPI     | No real wire transport                              |
| Logic-analyzer FW    | Not linked or included                              |
| Oscilloscope FW      | Not linked or included                              |
| Pico SDK (direct)    | Pulled in transitively only through `picomso_common`|

### Control-Plane Flow

```
[ Host packet bytes ]
        │
        ▼
  transport_receive()          ← dummy_transport_iface.receive()
        │                         copies pre-loaded rx_buf bytes
        ▼
  picomso_dispatch()           ← validates header, routes to handler
        │                         handler reads/writes capture_controller_t
        ▼
  transport_send()             ← dummy_transport_iface.send()
        │                         stores response in tx_buf
        ▼
[ Response bytes in tx_buf ]
```

The `capture_controller_t` instance that `GET_STATUS` and `SET_MODE` read
and write lives inside `firmware/protocol/src/protocol_dispatch.c`.  The
integration layer never touches it directly; all control-plane mutations go
through `picomso_dispatch()`.

### Usage Sketch

```c
/* 1. Prepare the dummy transport. */
dummy_transport_state_t  dummy_state;
transport_ctx_t          transport;
dummy_transport_init(&dummy_state);
transport_init(&transport, &dummy_transport_iface, &dummy_state);

/* 2. Initialise the integration context. */
integration_ctx_t ctx;
integration_init(&ctx, &transport);

/* 3. Pre-load an incoming packet (e.g. a GET_STATUS request). */
const uint8_t packet[] = { ... };
dummy_transport_set_rx(&dummy_state, packet, sizeof(packet));

/* 4. Process one packet: receive → dispatch → send. */
integration_process_one(&ctx);

/* 5. Inspect the response. */
size_t resp_len;
const uint8_t *resp = dummy_transport_get_tx(&dummy_state, &resp_len);
```

---

## Build Entry Points

Both projects remain independently buildable. The shared library in
`firmware/common/` is included as a CMake subdirectory by each project:

```cmake
add_subdirectory(../../firmware/common ${CMAKE_BINARY_DIR}/picomso_common)
```

```bash
# Logic Analyzer
cmake -S logic_analyzer_rp2040/src -B build/logic_analyzer
cmake --build build/logic_analyzer

# Oscilloscope
cmake -S oscilloscope_rp2040/src -B build/oscilloscope
cmake --build build/oscilloscope
```

The `firmware/CMakeLists.txt` entry point (which builds all sub-libraries
together) is reserved for future mixed-signal integration and is not used by
the individual project builds.

---

## USB Transport Backend (`firmware/transport/usb/`)

### Origin

The PicoMSO USB transport backend is a direct adaptation of the custom RP2040
USB library already used in the `oscilloscope_rp2040` codebase.  It does **not**
use TinyUSB or any other external USB stack.  It drives the RP2040 USB hardware
directly through the `hardware/structs/usb.h` register map provided by the
Pico SDK.

### File Roles

| File | Role | Classification |
|------|------|----------------|
| `usb_common.h` | USB standard constants, packed descriptor structs (`usb_setup_packet`, `usb_device_descriptor`, etc.) | **Generic USB library** |
| `usb.h` | Public API declarations: `usb_device_init`, `usb_is_configured`, `usb_init_transfer`, `usb_continue_transfer`, `usb_cancel_transfer`, `usb_get_endpoint_configuration`, `usb_get_address` | **Generic USB library** |
| `usb.c` | USB device driver: IRQ handler (`isr_usbctrl`), endpoint setup, descriptor handling, buffer management | **Generic USB library** |
| `usb_config.h` | PicoMSO endpoint addresses, packet-size constants, `usb_endpoint_configuration` and `usb_device_configuration` structs; static USB descriptors with PicoMSO product strings | **PicoMSO-specific configuration** |
| `usb_config.c` | Instantiates `dev_config` (the `usb_device_configuration`) and `ep0_buf`; forward-declares `control_transfer_handler` | **PicoMSO-specific configuration** |

`usb_config.c` is `#include`d directly by `usb.c` (not compiled as a separate
translation unit).  This matches the design used in `oscilloscope_rp2040`.

### Separation Principle

- **Generic USB library (`usb.c`, `usb.h`, `usb_common.h`)** — contains no
  project-specific strings, IDs, or endpoint counts.  These files can be reused
  unchanged for any RP2040 USB device that adopts this architecture.
- **PicoMSO-specific configuration (`usb_config.h`, `usb_config.c`)** — defines
  the PicoMSO USB descriptor tree (vendor/product strings, VID/PID, endpoint
  layout).  Adapting for a new device requires changing only these two files.

### Application Responsibility

The application layer that links against `picomso_usb_transport` must define:

```c
void control_transfer_handler(uint8_t *buf,
                               volatile struct usb_setup_packet *pkt,
                               uint8_t stage);
```

This callback is invoked by the USB driver on every control transfer stage
(SETUP, DATA, STATUS).  It is how the application layer handles vendor-specific
USB commands.

### CMake Target

```cmake
# Provided by firmware/transport/usb/CMakeLists.txt
# Requires Pico SDK to be initialised by the parent project.
target_link_libraries(my_app picomso_usb_transport)
```

The target links `hardware_irq`, `hardware_resets`, and `pico_stdlib`.
It does **not** link TinyUSB or any other USB middleware.

### Build Limitations

`picomso_usb_transport` is guarded by `PICO_SDK_INITIALIZED` in
`firmware/transport/CMakeLists.txt`.  It is therefore only built when the
parent project has called `pico_sdk_init()`.  The existing
`logic_analyzer_rp2040` and `oscilloscope_rp2040` entry points are not
affected; neither includes `firmware/transport/usb/` in their build graphs.

The USB transport backend is not yet wired to `picomso_transport`,
`picomso_integration`, or any capture pipeline.  That wiring is deferred to
a future phase when a concrete PicoMSO firmware entry point is added.
