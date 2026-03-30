# firmware/mixed_signal

Home for acquisition and capture logic used by the new firmware path.

Current contents:

- `logic_capture.c` / `logic_capture.h` – a concrete one-shot logic-analyzer
  capture backend for the `firmware/examples/usb_control_plane` flow
- `scope_capture.c` / `scope_capture.h` – a concrete one-shot oscilloscope
  capture backend that mirrors the same request/complete/readout flow

Current scope is intentionally narrow:

- logic-analyzer and oscilloscope one-shot capture only
- request-defined total capture length
- completed-capture storage plus fixed-size readout through the existing protocol/USB path
- circular pre-trigger buffering and trigger detection for the logic path
- finite oscilloscope acquisition without a separate analog-trigger algorithm

Still out of scope here:

- mixed-signal unification
- continuous/live streaming capture
- a separate oscilloscope-specific trigger algorithm
- shared logic/scope abstractions beyond what the current one-shot paths need
