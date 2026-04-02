# firmware/protocol

Transport-agnostic PicoMSO protocol layer for the new firmware path.

Current responsibilities:

- packet framing and validation
- command dispatch
- control-plane state reporting
- logic capture request handling (`REQUEST_CAPTURE`)
- finalized capture chunk readout (`READ_DATA_BLOCK`)

## Debug logging

The protocol path now emits structured debug traces with the `[protocol]`
prefix so the control-plane flow can be reconstructed from the top level.

Key trace points include:

- packet validation failures and rejected commands
- incoming command dispatch (`dispatch.rx`) and response completion (`dispatch.tx`)
- `SET_MODE` requests, previous stream/state, and applied mode
- `REQUEST_CAPTURE` validation, selected backend, and start result
- backend completion callbacks returning the controller to `IDLE`
- `GET_STATUS` decisions, including controller state before sync, backend state,
  and reported result
- `READ_DATA_BLOCK` attempts and per-block read results

This layer stays independent from USB implementation details. It is wired to
the validated USB backend through `firmware/integration/`, and delegates only
the concrete logic-capture work to `firmware/mixed_signal/`.
