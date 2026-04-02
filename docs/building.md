# Building

## Pico SDK submodule

The firmware build uses the Pico SDK submodule at:

```text
external/pico-sdk
```

Initialize submodules before configuring the build:

```bash
git submodule update --init --recursive
```

## Current firmware build

The current PicoMSO application entry point is `firmware/app/`.

Build from the repository root:

```bash
cmake -S firmware/app -B build/picomso
cmake --build build/picomso
```

Primary outputs:

- `build/picomso/picomso.elf`
- `build/picomso/picomso.uf2`

## Firmware libraries

`firmware/app/CMakeLists.txt` adds these directories as subprojects:

- `firmware/common/`
- `firmware/mixed_signal/`
- `firmware/protocol/`
- `firmware/transport/`
- `firmware/integration/`

These directories are libraries used by the application build. They are not
documented as standalone firmware entry points.

`firmware/CMakeLists.txt` collects the same libraries, but it is not a complete
top-level build on its own because the Pico SDK must already be initialized by a
parent project.

## Hardware validation

`utils/usb_test.py` is a standalone hardware-facing validation script. It
requires:

- Python with `pyusb`
- a connected PicoMSO device

The repository does not include a separate automated documentation test suite.
