# PicoMSO libsigrok-style host driver

This directory contains a minimal PicoMSO host driver implementation structured in a
libsigrok-style split between:

- `api.c` for device lifecycle and acquisition flow
- `protocol.c` / `protocol.h` for PicoMSO packet encoding and decoding

Scope is intentionally limited to PicoMSO logic-analyzer mode:

- 16 digital channels (`D0` .. `D15`)
- 16-bit little-endian samples
- no host-side trigger emulation
- variable finalized capture length accepted as normal firmware behavior
- `READ_DATA_BLOCK` end-of-capture is detected from the firmware's
  `"no finalized capture data"` response

This code is kept self-contained inside the repository and does not require the
full libsigrok tree to build.

## Build and test

```sh
cmake -S host/libsigrok/picomso -B build/picomso_sigrok_driver
cmake --build build/picomso_sigrok_driver
ctest --test-dir build/picomso_sigrok_driver --output-on-failure
```
