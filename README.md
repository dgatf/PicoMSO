# PicoMSO

**RP2040-based Mixed-Signal Oscilloscope (MSO)**  
An incremental unification of the imported logic analyzer and oscilloscope firmware for a future mixed-signal RP2040 project.

> **Current status:** The working firmware still lives in `logic_analyzer_rp2040/` and `oscilloscope_rp2040/`.
> The new `firmware/` directory is scaffolding only for future shared modules and does not replace the existing build entry points yet.

---

## Overview

PicoMSO currently starts from two existing firmware codebases that remain buildable and reviewable in their original locations:

- `logic_analyzer_rp2040/` - imported 16-channel logic analyzer firmware
- `oscilloscope_rp2040/` - imported oscilloscope firmware

The repository is being evolved toward a shared mixed-signal architecture, but the transition is intentionally incremental. Existing source trees stay in place until shared code boundaries are clear enough to extract safely.

---

## Current Repository State

The repository currently contains:

- the original imported firmware trees at the top level
- a new `firmware/` area reserved for shared code and future mixed-signal support
- a `docs/` area for transition planning and build guidance

The current firmware build entry points are unchanged:

- `logic_analyzer_rp2040/src/CMakeLists.txt`
- `oscilloscope_rp2040/src/CMakeLists.txt`

---

## Transitional Repository Structure

```text
PicoMSO/
├── README.md
├── docs/
│   ├── building.md
│   └── repository-transition.md
├── firmware/
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── common/
│   │   ├── CMakeLists.txt
│   │   └── README.md
│   ├── mixed_signal/
│   │   ├── CMakeLists.txt
│   │   └── README.md
│   └── protocol/
│       ├── CMakeLists.txt
│       └── README.md
├── logic_analyzer_rp2040/
└── oscilloscope_rp2040/
```

This structure keeps the imported projects intact while creating clearly named destinations for future shared code.

---

## Shared Components Identified So Far

The two imported firmware trees already show a few common patterns that are good candidates for future extraction:

- debug logging support in each project's `common.c` / `common.h`
- boot-time GPIO configuration patterns
- status LED behaviour during startup and capture
- RP2040/Pico SDK based CMake build flow

Other areas are similar in purpose but not yet safe to merge without deeper refactoring:

- protocol handling (`protocol_sump.c` vs `protocol.c`)
- capture backends (`capture.c` vs `oscilloscope.c`)
- transport layers (UART vs USB)

For now, those areas remain in place and are documented rather than moved.

---

## Migration Principles

- Keep existing folders and build scripts working during the transition
- Prefer wrappers, adapters, and documentation before moving source files
- Extract shared code only when include and dependency impact is clear
- Make small, reviewable changes instead of large repository reshuffles

---

## Next Documentation

- Transition plan: [`docs/repository-transition.md`](docs/repository-transition.md)
- Build guidance: [`docs/building.md`](docs/building.md)
