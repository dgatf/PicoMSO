# firmware/protocol

Transport-agnostic PicoMSO protocol layer for the new firmware path.

Current responsibilities:

- packet framing and validation
- command dispatch
- control-plane state reporting
- logic capture request handling (`REQUEST_CAPTURE`)
- finalized capture chunk readout (`READ_DATA_BLOCK`)

This layer stays independent from USB implementation details. It is wired to
the validated USB backend through `firmware/integration/`, and delegates only
the concrete logic-capture work to `firmware/mixed_signal/`.
