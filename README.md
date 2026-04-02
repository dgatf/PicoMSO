# PicoMSO

PicoMSO is an RP2040-based mixed-signal instrument that combines logic-analyzer
and oscilloscope functionality in a single firmware and host integration stack.

## Specifications

### Logic analyzer

- **Channels:** 16 digital channels
- **Maximum sample rate:** up to **200 MHz**
- **Capture depth:** up to **50K samples**
- **Pre-trigger buffer:** up to **1K samples**
- **Trigger support:** level and edge trigger support

### Oscilloscope

- **Channels:** 1 analog channel
- **Maximum sample rate:** up to **2 MS/s**

## PulseView example

The screenshot below shows PicoMSO running in PulseView with digital and analog
data displayed in the same session.

![PicoMSO PulseView example](docs/images/picomso-pulseview.png)

## Build

Initialize submodules, then build the firmware application from the repository
root:

```bash
git submodule update --init --recursive
cmake -S firmware/app -B build/picomso
cmake --build build/picomso
```

Additional documentation:

- [`docs/building.md`](docs/building.md)
- [`docs/architecture.md`](docs/architecture.md)
- [`docs/protocol.md`](docs/protocol.md)

## libsigrok support

Until PicoMSO support is merged upstream, users need to build and install the
PicoMSO fork of `libsigrok` manually. Please follow the [official sigrok build
instructions](https://sigrok.org/wiki/Building) and apply them to the PicoMSO
fork repository: [`https://github.com/dgatf/libsigrok`](https://github.com/dgatf/libsigrok).

The sigrok documentation covers the required dependencies, general build flow,
and platform-specific notes for building from source.

## Status

PicoMSO is currently feature-complete and functional, with final validation,
cleanup, and documentation polish still in progress.