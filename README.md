
========================
FILE: README.md
========================

# PicoMSO

**RP2040-based Mixed-Signal Oscilloscope (MSO)**  
Logic Analyzer + Oscilloscope with a unified firmware architecture and future PulseView / libsigrok support.

---

## Overview

PicoMSO is an open-source mixed-signal instrument built on the RP2040.

It combines:

- Logic Analyzer (multi-channel digital capture)
- Oscilloscope (analog signal capture)
- Shared timebase for synchronized mixed-signal analysis

The goal is to evolve into a single device compatible with PulseView (libsigrok).

---

## Planned Features

- Up to 16 digital channels
- 1 analog channel (initial mixed-signal design)
- Mixed-signal capture (digital + analog)
- Shared timebase across all channels
- Trigger support (incremental)
- Pre-trigger / post-trigger capture

---

## Initial Scope

The first version is intentionally simple:

- finite buffered captures
- no continuous streaming
- block-based transfer after capture
- protocol designed for libsigrok compatibility

This aligns with PulseView usage, which is based on finite captures.

---

## Project Structure

```text
pico-mso/
├── firmware/
│   ├── common/
│   ├── logic_analyzer/
│   ├── oscilloscope/
│   ├── mixed_signal/
│   ├── protocol/
│   └── transport/
├── docs/
│   ├── protocol.md
│   ├── architecture.md
│   └── mixed_signal_design.md
├── host/
└── README.md
```

---

## Architecture

```text
PulseView / libsigrok
          ↑
   (future driver)
          ↑
   Device Protocol
          ↑
      Transport
          ↑
  Capture Controller
   ├── Logic Backend
   ├── Analog Backend
   └── Mixed Coordinator
```

---

## Mixed-Signal Design Principles

- Shared logical timebase
- Coordinated capture start
- Shared metadata
- No need for unified DMA
- DMA is backend-specific
- Digital and analog use separate data blocks
- Alignment handled via metadata

---

## Planned Limits

- Digital is primary capability
- Analog is complementary
- Mixed mode has reduced limits
- No streaming in v1
- No strict cycle-level sync required
- External behavior must be temporally coherent

---

## Roadmap

### Phase 1
- Import existing code
- Extract common modules
- Define protocol

### Phase 2
- Capture controller
- Logic mode
- Analog mode

### Phase 3
- Mixed mode (16 digital + 1 analog)
- Timebase alignment
- Metadata validation

### Phase 4
- libsigrok driver
- PulseView integration

---

## Status

Early development

---

## Design Principles

- Correctness over performance
- Incremental refactoring
- Simple architecture
- DMA is optional
- Finite capture first

---

## Hardware

- RP2040 (Raspberry Pi Pico)

---

## License

TBD


========================
FILE: docs/protocol.md
========================

# PicoMSO Device Protocol

## Overview

The PicoMSO protocol is designed for:

- finite buffered capture
- block-based data transfer
- mixed-signal alignment
- future libsigrok driver compatibility

Streaming is explicitly out of scope for the initial version.

---

## Capture Model

The device operates in discrete capture cycles:

1. Configure device
2. ARM capture
3. Wait for trigger
4. Capture into RAM
5. Transfer data to host

---

## Metadata

All captures must expose explicit metadata:

```c
capture_metadata {
    samplerate_hz
    total_samples
    trigger_index

    digital_channel_count
    analog_channel_count

    digital_enable_mask
    analog_enable_mask
}
```

This metadata defines the global timeline used by the host.

---

## Timebase Rules

- All channels share the same logical samplerate
- Digital and analog data must align to the same timeline
- trigger_index must be valid for all channels

The host must not infer alignment — it must be explicit.

---

## Data Format

### Digital Samples

```c
uint16_t sample;
```

Each sample represents up to 16 digital channels.

---

### Analog Samples

```c
uint16_t sample;
```

Initial implementation uses a single analog channel.

---

## Data Transfer

Data is transferred in separate blocks:

```text
[DIGITAL BLOCKS]
[ANALOG BLOCKS]
```

Each block includes:

- capture_id
- block_index
- sample_index
- payload

---

## Key Principle

Mixed-signal correctness is achieved through:

- shared timebase
- consistent metadata
- aligned sample counts

Not through hardware-level synchronization.

---

## Design Decisions

- No streaming support in v1
- No requirement for shared DMA
- Protocol is transport-agnostic
- Backend implementation details are hidden

---

## Future Extensions

- RLE compression (digital)
- multi-channel analog
- streaming mode
- advanced triggers

---

## Summary

The protocol prioritizes:

- simplicity
- deterministic behavior
- correct alignment

over maximum performance.


========================
FILE: docs/architecture.md
========================

# PicoMSO Firmware Architecture

## Overview

PicoMSO combines:

- logic analyzer functionality
- oscilloscope functionality
- mixed-signal capture

The architecture is designed to unify both systems while keeping them modular.

---

## Core Principle

Mixed-signal synchronization is achieved through:

- shared logical timebase
- coordinated capture start
- shared metadata

NOT through shared DMA or tightly coupled pipelines.

---

## Architecture

```text
Protocol
   ↓
Capture Controller
   ↓
Backends
   ├── Logic Backend
   ├── Analog Backend
   └── Mixed Coordinator
```

---

## Capture Controller

Responsibilities:

- manage device state
- apply configuration
- coordinate capture lifecycle
- provide unified interface to protocol

States:

- idle
- configured
- armed
- capturing
- complete

---

## Backends

### Logic Backend

- digital sampling
- optional DMA usage
- independent implementation

---

### Analog Backend

- ADC-based sampling
- optional DMA usage
- independent implementation

---

## Mixed Coordinator

Responsibilities:

- ensure shared timeline
- unify metadata
- coordinate start

Non-responsibilities:

- low-level sampling
- DMA control

---

## Buffer Model

- capture stored in RAM
- transferred in blocks
- no streaming required

---

## DMA Policy

DMA is:

- optional
- backend-specific
- not part of shared architecture

---

## Design Rules

- protocol independent of hardware
- backends independent
- metadata is the source of truth
- correctness over performance

---

## Key Insight

PulseView requires:

- coherent timeline
- consistent metadata

It does NOT require:

- perfect cycle-level synchronization
- shared DMA pipeline

---

## Summary

The architecture focuses on:

- simplicity
- modularity
- deterministic behavior

and enables gradual evolution toward a full mixed-signal device.

