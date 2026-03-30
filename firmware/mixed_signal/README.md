# firmware/mixed_signal

Home for acquisition and capture logic used by the new firmware path.

Current contents:

- `logic_capture.c` / `logic_capture.h` – a concrete one-shot logic-analyzer
  capture backend for the `firmware/examples/usb_control_plane` flow

Current scope is intentionally narrow:

- logic-analyzer capture only
- request-defined total capture length
- circular pre-trigger buffering
- trigger detection
- finite post-trigger acquisition
- finalized one-shot storage plus fixed-size readout through the existing protocol/USB path

Still out of scope here:

- oscilloscope capture
- mixed-signal unification
- shared logic/scope abstractions beyond what the logic path needs today
