# PicoMSO Repository Transition

## Goal

Evolve the repository from two imported RP2040 firmware projects into a unified PicoMSO mixed-signal project without breaking the current codebases early in the transition.

---

## Current Imported Structure

The repository currently contains two imported firmware projects at the top level:

```text
PicoMSO/
├── logic_analyzer_rp2040/
│   └── src/
│       ├── CMakeLists.txt
│       ├── capture.c
│       ├── common.c
│       ├── common.h
│       ├── main.c
│       └── protocol_sump.c
└── oscilloscope_rp2040/
    └── src/
        ├── CMakeLists.txt
        ├── common.c
        ├── common.h
        ├── main.c
        ├── oscilloscope.c
        ├── protocol.c
        ├── usb.c
        └── usb_config.c
```

These imported projects remain the active firmware build entry points during the transition.

---

## Shared Components Identified

### Strong candidates for future sharing

- `logic_analyzer_rp2040/src/common.c`
- `logic_analyzer_rp2040/src/common.h`
- `oscilloscope_rp2040/src/common.c`
- `oscilloscope_rp2040/src/common.h`

These files both provide debug/logging support and related utility definitions, but they are not identical yet. They should be extracted only after the remaining type and initialization differences are reconciled.

### Shared architectural patterns

- RP2040 + Pico SDK CMake build flow
- boot-time GPIO configuration
- status indication and debug enable conventions
- per-project protocol layer above a hardware capture backend

### Not yet safe to merge

- logic capture backend (`capture.c`, `capture.pio`)
- oscilloscope capture backend (`oscilloscope.c`)
- transport/protocol implementations (`protocol_sump.c`, `protocol.c`, `usb.c`)

Those modules have different hardware assumptions and host-facing behavior, so they stay in place for now.

---

## Proposed Transitional Structure

The transition starts by adding shared destinations without moving the imported projects yet:

```text
PicoMSO/
├── docs/
│   ├── building.md
│   └── repository-transition.md
├── firmware/
│   ├── common/
│   ├── protocol/
│   └── mixed_signal/
├── logic_analyzer_rp2040/
└── oscilloscope_rp2040/
```

### Purpose of the new folders

- `firmware/common/`  
  Planned home for code that is truly shared across the existing firmware trees.

- `firmware/protocol/`  
  Planned home for future protocol abstractions, adapters, or reusable framing helpers.

- `firmware/mixed_signal/`  
  Planned home for shared coordination logic once mixed-signal capture is implemented.

- `docs/`  
  Home for the transition plan, build notes, and architecture documentation.

At this stage, the folders are intentionally scaffolding only. Existing code is not moved into them yet.

---

## Final Target Structure

Once the dependency boundaries are better understood, the repository can converge toward a unified layout similar to:

```text
PicoMSO/
├── docs/
├── firmware/
│   ├── common/
│   ├── logic_analyzer/
│   ├── oscilloscope/
│   ├── mixed_signal/
│   └── protocol/
├── logic_analyzer_rp2040/      # optional compatibility wrapper or retired import
└── oscilloscope_rp2040/        # optional compatibility wrapper or retired import
```

That final structure should only be introduced after shared modules are stable and the original build paths have clear replacements.

---

## Recommended Migration Order

1. **Document the current imports and the intended destination layout**  
   Establish the shared folder structure and clarify that the imported projects still build from their original locations.

2. **Preserve both existing build entry points**  
   Keep `logic_analyzer_rp2040/src/CMakeLists.txt` and `oscilloscope_rp2040/src/CMakeLists.txt` unchanged until shared extraction work is ready.

3. **Extract low-risk shared utilities first**  
   Start with narrow helpers such as debug/logging utilities only after their type and initialization differences are resolved.

4. **Introduce wrappers/adapters before deep moves**  
   If new shared APIs are needed, add adapters in the legacy trees first so source moves can remain mechanical and reviewable.

5. **Move protocol or mixed-signal coordination code only after interfaces stabilize**  
   Transport, capture, and host protocol logic have larger behavioral impact and should move later.

6. **Retire or reduce the imported top-level trees last**  
   Only after the new `firmware/` structure owns the real build targets should the legacy layout become a compatibility layer or be removed.

---

## Build Impact in This Step

- Existing imported firmware trees remain the active build entry points
- No source files are moved between codebases in this step
- New `firmware/` build files are scaffolding only and do not replace the current project builds

