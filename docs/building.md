# Building

## Pico SDK

This repository includes the Pico SDK as a Git submodule at:

```text
external/pico-sdk
```

Before building, initialize the submodule from the repository root:

```bash
git submodule update --init --recursive
```

The `firmware/examples/usb_control_plane/` project uses this submodule directly
and no longer depends on `PICO_SDK_PATH`.

---

## Firmware Build Entry Points

Both firmware projects build from their own source trees. Each one pulls in
`firmware/common/` as a CMake subdirectory, so no separate step is required to
build the shared library first.

### Logic Analyzer

```bash
cmake -S logic_analyzer_rp2040/src -B build/logic_analyzer
cmake --build build/logic_analyzer
```

Produces `build/logic_analyzer/logic_analyzer.elf` and
`build/logic_analyzer/logic_analyzer.uf2`.

### Oscilloscope

```bash
cmake -S oscilloscope_rp2040/src -B build/oscilloscope
cmake --build build/oscilloscope
```

Produces `build/oscilloscope/oscilloscope.elf` and
`build/oscilloscope/oscilloscope.uf2`.

---

## Shared Library (`firmware/common`)

`firmware/common/` is included automatically by both project builds via:

```cmake
add_subdirectory(../../firmware/common ${CMAKE_BINARY_DIR}/picomso_common)
```

It is **not** intended to be built standalone because it depends on Pico SDK
targets (`pico_stdlib`, `hardware_uart`, `hardware_gpio`) that are only
available once `pico_sdk_init()` has been called by a project build.

The placeholder `firmware/protocol/` and `firmware/mixed_signal/` subdirectories
remain INTERFACE-only libraries reserved for future use.

---

## USB Control-Plane Example (`firmware/examples/usb_control_plane/`)

A minimal, self-contained Pico SDK project that shows the complete wiring
of USB transport → integration → protocol → capture_controller.

This example uses the Pico SDK submodule at `external/pico-sdk`.

```bash
git submodule update --init --recursive
cmake -S firmware/examples/usb_control_plane \
      -B build/usb_control_plane
cmake --build build/usb_control_plane
```

Produces:

- `build/usb_control_plane/picomso_usb_control_plane.elf`
- `build/usb_control_plane/picomso_usb_control_plane.uf2`

Commands handled: `GET_INFO`, `GET_CAPABILITIES`, `GET_STATUS`, `SET_MODE`.
Capture data streaming is **not** implemented in this example.

---

## Migration Guidance

- Code in `firmware/common/` is real, shared, Pico SDK-dependent source
- Both project builds link against `picomso_common` and inherit its include paths
- Add to `firmware/common/` only when code is clearly identical across both projects
- Protocol handlers, capture backends, and transport layers remain project-specific