# Building During the Transition

## Current Build Entry Points

During the transition, the existing firmware projects continue to build from their original imported source trees:

- `logic_analyzer_rp2040/src/CMakeLists.txt`
- `oscilloscope_rp2040/src/CMakeLists.txt`

This keeps include paths, source layout, and project-specific build assumptions unchanged while the shared architecture is still being defined.

---

## Existing Firmware Builds

### Logic analyzer

```bash
cmake -S logic_analyzer_rp2040/src -B build/logic_analyzer
cmake --build build/logic_analyzer
```

### Oscilloscope

```bash
cmake -S oscilloscope_rp2040/src -B build/oscilloscope
cmake --build build/oscilloscope
```

Both builds require a valid Pico SDK setup, including `PICO_SDK_PATH`.

---

## Transitional Firmware Scaffold

The new `firmware/` tree now has a lightweight CMake scaffold so shared modules can be added incrementally without changing the imported projects first.

```bash
cmake -S firmware -B build/firmware
cmake --build build/firmware
```

At this stage, that scaffold only defines placeholder interface libraries:

- `picomso_firmware_common`
- `picomso_firmware_protocol`
- `picomso_firmware_mixed_signal`

These placeholders provide named destinations for future shared code without changing the current firmware binaries.

---

## Migration Guidance

- Do not move code into `firmware/common/` until dependency impact is understood
- Do not redirect existing project CMake files to the new scaffold prematurely
- Prefer adding wrappers or small shared helpers before consolidating major modules

