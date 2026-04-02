# firmware/common

Shared PicoMSO runtime utilities.

## Contents

### `include/debug.h` + `src/debug.c`

UART-based debug logging helpers used by the current firmware stack.

### `include/types.h`

Shared type definitions, including:

- `capture_state_t`
- `trigger_match_t`
- `trigger_t`
- `capture_config_t`

### `include/capture_controller.h` + `src/capture_controller.c`

Shared controller state used by the protocol layer.

`capture_controller_t` tracks:

- `streams_enabled`
- `state`

Supported stream bits:

- `PICOMSO_STREAM_NONE`
- `PICOMSO_STREAM_LOGIC`
- `PICOMSO_STREAM_SCOPE`

## CMake usage

`firmware/app/CMakeLists.txt` adds this directory and links the resulting
`picomso_common` library into the current firmware build.

## Scope

Place code here only when it is shared by the current PicoMSO firmware path and
does not belong to a transport-specific or capture-backend-specific module.
