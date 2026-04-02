# firmware

Current PicoMSO firmware implementation.

## Layout

- `app/` - Pico SDK application entry point
- `common/` - shared runtime support
- `integration/` - transport/protocol glue
- `mixed_signal/` - logic and scope capture backends
- `protocol/` - packet definitions and dispatch
- `transport/` - transport abstraction and USB backend

`firmware/app/` is the current build entry point. The other directories provide
libraries linked by that application.
