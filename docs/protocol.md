# PicoMSO Protocol

This document describes the current protocol implemented under
`firmware/protocol/`.

## Versioning

| Constant | Value |
|---|---|
| `PICOMSO_PROTOCOL_VERSION_MAJOR` | `0` |
| `PICOMSO_PROTOCOL_VERSION_MINOR` | `3` |

## Packet header

Every packet starts with an 8-byte little-endian header:

| Offset | Size | Field |
|---|---:|---|
| 0 | 2 | `magic` (`0x4D53`) |
| 2 | 1 | `version_major` |
| 3 | 1 | `version_minor` |
| 4 | 1 | `msg_type` |
| 5 | 1 | `seq` |
| 6 | 2 | `length` |

Maximum payload length is 512 bytes.

## Message types

| Value | Constant | Direction |
|---|---|---|
| `0x01` | `PICOMSO_MSG_GET_INFO` | host -> device |
| `0x02` | `PICOMSO_MSG_GET_CAPABILITIES` | host -> device |
| `0x03` | `PICOMSO_MSG_GET_STATUS` | host -> device |
| `0x04` | `PICOMSO_MSG_SET_MODE` | host -> device |
| `0x05` | `PICOMSO_MSG_REQUEST_CAPTURE` | host -> device |
| `0x06` | `PICOMSO_MSG_READ_DATA_BLOCK` | host -> device |
| `0x80` | `PICOMSO_MSG_ACK` | device -> host |
| `0x81` | `PICOMSO_MSG_ERROR` | device -> host |
| `0x82` | `PICOMSO_MSG_DATA_BLOCK` | device -> host |

## Status codes

| Value | Constant |
|---|---|
| `0x00` | `PICOMSO_STATUS_OK` |
| `0x01` | `PICOMSO_STATUS_ERR_UNKNOWN` |
| `0x02` | `PICOMSO_STATUS_ERR_BAD_MAGIC` |
| `0x03` | `PICOMSO_STATUS_ERR_BAD_LEN` |
| `0x04` | `PICOMSO_STATUS_ERR_BAD_MODE` |
| `0x05` | `PICOMSO_STATUS_ERR_VERSION` |

## Streams

The current control model uses a stream bitmask:

| Bit | Constant | Meaning |
|---|---|---|
| 0 | `PICOMSO_STREAM_LOGIC` | logic capture enabled |
| 1 | `PICOMSO_STREAM_SCOPE` | scope capture enabled |

`PICOMSO_STREAM_NONE` disables both streams. The implementation also accepts the
combined logic+scope mask.

## Command payloads

### `GET_INFO`

Request payload: none.

ACK payload:

| Offset | Size | Field |
|---|---:|---|
| 0 | 1 | `protocol_version_major` |
| 1 | 1 | `protocol_version_minor` |
| 2 | 32 | `fw_id` |

Current firmware identifier: `PicoMSO-0.1`.

### `GET_CAPABILITIES`

Request payload: none.

ACK payload:

| Offset | Size | Field |
|---|---:|---|
| 0 | 4 | `capabilities` |

Capability bits:

- `PICOMSO_CAP_LOGIC`
- `PICOMSO_CAP_SCOPE`

### `GET_STATUS`

Request payload: none.

ACK payload:

| Offset | Size | Field |
|---|---:|---|
| 0 | 1 | `streams` |
| 1 | 1 | `capture_state` |

`capture_state` is `0x00` for idle and `0x01` for running.

### `SET_MODE`

Request payload:

| Offset | Size | Field |
|---|---:|---|
| 0 | 1 | `streams` |

The command name remains `SET_MODE`, but the payload is a stream bitmask.

On success the firmware:

- resets stored logic and scope capture state
- resets mixed-read tracking
- sets controller state to idle
- stores the requested stream mask

### `REQUEST_CAPTURE`

Request payload:

| Offset | Size | Field |
|---|---:|---|
| 0 | 4 | `total_samples` |
| 4 | 4 | `rate` |
| 8 | 4 | `pre_trigger_samples` |
| 12 | 12 | `trigger[4]` |

Each trigger entry is:

| Offset within entry | Size | Field |
|---|---:|---|
| 0 | 1 | `is_enabled` |
| 1 | 1 | `pin` |
| 2 | 1 | `match` |

The firmware validates the payload length, stream selection, trigger entries,
and capture sizing before starting capture.

Current behavior:

- logic-only stream selection starts the logic backend
- scope-only stream selection starts the scope backend
- combined stream selection starts both backends

### `READ_DATA_BLOCK`

Request payload: none.

`PICOMSO_MSG_DATA_BLOCK` payload:

| Offset | Size | Field |
|---|---:|---|
| 0 | 1 | `stream_id` |
| 1 | 1 | `flags` |
| 2 | 2 | `block_id` |
| 4 | 2 | `data_len` |
| 6 | `data_len` | `data` |

Current stream IDs:

- `1` = logic
- `2` = scope

In combined-stream operation, readout alternates between logic and scope when
data is available and uses `stream_id` to identify each returned block.

## Transport split

With the current USB backend:

- requests arrive on EP0 as vendor OUT control transfers
- `PICOMSO_MSG_DATA_BLOCK` responses are sent on EP6 IN

The protocol layer itself remains transport-agnostic and operates only on raw
buffers.

## Current limitations

- no live streaming during acquisition
- static firmware identifier
- static capabilities bitmap
- fixed 64-byte block readout from finalized capture buffers
- scope capture uses the same request shape as logic capture
