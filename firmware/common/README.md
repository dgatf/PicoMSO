# firmware/common

Shared low-level firmware utilities for both `logic_analyzer_rp2040` and
`oscilloscope_rp2040`.

## Contents

### `include/debug.h` + `src/debug.c`

UART-based debug logging helpers, identical across both projects.  Both projects
previously contained near-duplicate `common.c` implementations; this is the
single authoritative copy.

- `debug_init(baudrate, buffer, is_enabled)` – initialise UART0 when debug is on
- `debug_reinit()` – re-initialise after a clock-rate change
- `debug(fmt, ...)` – non-blocking formatted output
- `debug_block(fmt, ...)` – formatted output with TX-drain wait
- `debug_is_enabled()` – query current debug state

Pin assignments (both projects):

| Constant             | GPIO | Purpose               |
|----------------------|------|-----------------------|
| `DEBUG_UART_TX_GPIO` | 16   | UART0 TX output       |
| `DEBUG_UART_RX_GPIO` | 17   | UART0 RX (reserved)   |
| `DEBUG_ENABLE_GPIO`  | 18   | Active-low enable pin |

### `include/types.h`

Shared type definitions:

- `capture_state_t` – `CAPTURE_IDLE` / `CAPTURE_RUNNING`
- `trigger_match_t` – level/edge trigger kinds
- `trigger_t` – single-channel trigger configuration
- `capture_config_t` – logic-analyzer capture parameters

## CMake Usage

Each project adds this directory as a subdirectory and links against
`picomso_common`:

```cmake
add_subdirectory(../../firmware/common ${CMAKE_BINARY_DIR}/picomso_common)
target_link_libraries(my_target picomso_common)
```

## What Belongs Here

Extract code here only when it is **clearly identical or near-identical** across
both projects and carries no device-specific behaviour. See
`docs/architecture.md` for details on what remains project-specific and why.

