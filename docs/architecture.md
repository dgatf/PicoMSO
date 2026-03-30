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
`GET_STATUS`, and `SET_MODE`.  Handlers currently return static /
placeholder values; future phases will wire them to real hardware state.

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

`firmware/protocol/` does not currently call `capture_controller_t`
directly.  The `GET_STATUS` and `SET_MODE` handlers maintain simple
module-level state variables as placeholders.

In a future phase the dispatcher will accept a `capture_controller_t *`
context pointer (or equivalent) so that `GET_STATUS` can read the real
mode and capture state, and `SET_MODE` can delegate to
`capture_controller_set_mode()`.

### What Remains Unchanged

- `logic_analyzer_rp2040` continues to use its SUMP-over-CDC protocol.
- `oscilloscope_rp2040` continues to use its custom USB bulk protocol.
- Neither project includes or links `firmware/protocol/`.
- No capture pipeline, DMA setup, or USB framing has been modified.

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
