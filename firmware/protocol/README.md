# firmware/protocol

Planned home for future shared protocol helpers and adapter layers.

The current imported projects use different host-facing protocols and transports:

- SUMP over UART in `logic_analyzer_rp2040/`
- OpenHantek-style USB protocol in `oscilloscope_rp2040/`

Because those interfaces are not directly interchangeable, this directory starts as scaffolding only.

