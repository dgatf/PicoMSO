# PicoMSO

PicoMSO is an RP2040-based mixed-signal instrument that combines logic-analyzer
and oscilloscope functionality in a single firmware and host integration stack.

## Specifications

### Logic analyzer

- **Channels:** 16 digital channels
- **Maximum sample rate:** up to **200 MHz**
- **Capture depth:** up to **50 ksamples**
- **Pre-trigger buffer:** up to **1 ksample**
- **Trigger support:** level and edge triggers

### Oscilloscope

- **Channels:** 1 analog channel
- **Maximum sample rate:** up to **2 MS/s**

## PulseView example

The screenshot below shows PicoMSO running in PulseView with digital and analog
data displayed in the same session.

![PicoMSO PulseView example](docs/images/picomso-pulseview.png)

## libsigrok command-line examples

The examples below use `sigrok-cli` with the PicoMSO driver.

### Show device information

List detected devices:

```bash
sigrok-cli --scan
```

Show PicoMSO device information and supported options:

```bash
sigrok-cli -d picomso --show
```

### Capture logic data

Capture 1000 logic samples at 5 kHz from the default enabled logic channels:

```bash
sigrok-cli -d picomso --samples 1000 --config samplerate=5k
```

Capture logic data with a rising-edge trigger on channel `D0`:

```bash
sigrok-cli -d picomso --samples 1000 --config samplerate=5k --triggers D0=r
```

### Capture analog data

Capture 1000 analog samples from channel `A0` at 5 kHz:

```bash
sigrok-cli -d picomso --channels A0 --samples 1000 --config samplerate=5k
```

### Mixed-signal capture

Capture logic channel `D0` together with analog channel `A0` in the same session:

```bash
sigrok-cli -d picomso --channels D0,A0 --samples 1000 --config samplerate=5k
```

Capture mixed-signal data with a trigger on `D0`:

```bash
sigrok-cli -d picomso --channels D0,A0 --samples 1000 --config samplerate=5k --triggers D0=r
```

### Save capture data

Save a capture to a Sigrok session file:

```bash
sigrok-cli -d picomso --channels D0,A0 --samples 1000 --config samplerate=5k -o capture.sr
```

### Notes

- `samplerate` must be one of the rates reported by `sigrok-cli -d picomso --show`.
- `--samples` sets the capture length.
- `--triggers` currently applies to logic channels.
- Channel names are exposed as `D0` to `D15` for logic and `A0` for analog.

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

## Status

PicoMSO is functional, with final validation, cleanup, and documentation polish
still in progress.