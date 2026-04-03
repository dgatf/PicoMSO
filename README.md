# PicoMSO

PicoMSO is an RP2040-based mixed-signal instrument that combines logic-analyzer
and oscilloscope functionality in a single firmware and host integration stack.

## Specifications

### Logic analyzer

- **Channels:** 16 digital channels
- **Maximum sample rate:** up to **200 MHz**
- **Capture depth:** up to **50 ksamples**
- **Pre-trigger buffer:** up to **4 ksample**
- **Trigger support:** level and edge triggers

### Oscilloscope

- **Channels:** 1 analog channel
- **Maximum sample rate:** up to **2 MS/s**

## PulseView example

The screenshot below shows PicoMSO running in PulseView with digital and analog
data displayed in the same session.

![PicoMSO PulseView example](docs/images/picomso-pulseview.png)

## libsigrok command-line examples

Show PicoMSO device information:

```bash
sigrok-cli -d picomso --show
```

Capture logic data from channel `D0`:

```bash
sigrok-cli -d picomso --channels D0 --samples 1000 --config samplerate=5k
```

Capture mixed-signal data from logic channel `D0` and analog channel `A0`:

```bash
sigrok-cli -d picomso --channels D0,A0 --samples 1000 --config samplerate=5k
```

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
temporary PicoMSO `libsigrok` fork manually. Please follow the official sigrok
build instructions and apply them to the PicoMSO fork repository:
`https://github.com/dgatf/libsigrok`.

The sigrok documentation covers the required dependencies, general build flow,
and platform-specific notes for building from source.

## Signal integrity note

During validation, adding a series resistor of about **600 Ω** on the logic input helped suppress glitches and made trigger detection stable. If you observe spurious transitions or unreliable triggering, a small series resistor and, if needed, a simple RC filter may improve signal integrity.

## Status

PicoMSO is functional, with final validation, cleanup, and documentation polish
still in progress.
