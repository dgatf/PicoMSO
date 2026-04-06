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

- **Channels:** 1 or 2 analog channels in v1.0
- **Maximum sample rate:** up to **2 MS/s**
- **Capture depth:** up to **40 ksamples per enabled analog channel**
- **Resolution:** **12-bit** with 1 analog channel, **8-bit** internal capture with 2 analog channels

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

Capture two analog channels:

```bash
sigrok-cli -d picomso --channels A0,A1 --samples 1000 --config samplerate=5k
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

## Sample-rate limits

PicoMSO v1.0 supports both single-channel and dual-channel analog capture.
With 1 analog channel enabled, captures are 12-bit. With 2 analog channels
enabled, the ADC runs in round-robin mode across the enabled inputs, captures
each channel at 8-bit internally, and still returns samples through the
existing 16-bit scope stream format used by current host tools.

From the user's point of view, the configured analog samplerate and the
**40 ksamples** capture depth apply per enabled analog channel. The maximum
supported analog samplerate remains **2 MS/s**. Requests above this limit are
rejected by the driver with an argument error.

Logic-only captures can still use higher samplerates, up to **200 MHz**.

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
