# firmware/protocol

Transport-agnostic PicoMSO protocol layer.

## Responsibilities

- packet framing and validation
- command dispatch
- stream selection and status reporting
- capture request validation
- finalized data-block readout

## Commands

- `GET_INFO`
- `GET_CAPABILITIES`
- `GET_STATUS`
- `SET_MODE`
- `REQUEST_CAPTURE`
- `READ_DATA_BLOCK`

`SET_MODE` currently carries a stream bitmask payload. `READ_DATA_BLOCK`
responses include a `stream_id` so logic and scope data can be distinguished
when both streams are enabled.

## Integration

This layer operates on raw buffers. It is wired to transports through
`firmware/integration/` and delegates concrete capture work to
`firmware/mixed_signal/`.
