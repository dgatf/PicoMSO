# PicoMSO Protocol Specification (Phase 0)

This document describes the transport-agnostic protocol layer introduced in
`firmware/protocol/`.  The scope is intentionally limited: only four commands
are defined, the wire format is fully specified, and `GET_STATUS` / `SET_MODE`
now delegate to a real `capture_controller_t` instance instead of returning
static placeholders.

---

## Scope and Intentional Limitations

This phase establishes the **wire format**, **command vocabulary**, and
**control-plane state management** for the future unified PicoMSO host
protocol.  It does **not**:

- Replace the existing SUMP protocol used by `logic_analyzer_rp2040`.
- Replace the existing custom USB binary protocol used by `oscilloscope_rp2040`.
- Introduce any USB, CDC, bulk-endpoint, PIO, ADC, or DMA dependency.
- Implement the full device protocol.
- Integrate with libsigrok.
- Change any current firmware behaviour.

Both imported firmware projects remain independently buildable and unchanged.

---

## Versioning

| Constant                            | Value | Meaning                                         |
|-------------------------------------|-------|-------------------------------------------------|
| `PICOMSO_PROTOCOL_VERSION_MAJOR`    | `0`   | Bump on incompatible wire-format change         |
| `PICOMSO_PROTOCOL_VERSION_MINOR`    | `1`   | Bump when new commands are added                |

The device rejects any packet whose `version_major` differs from its own
with `PICOMSO_STATUS_ERR_VERSION`.

---

## Packet Header

Every packet (request or response) starts with an 8-byte fixed-length header.
All multi-byte fields are **little-endian**.

| Offset | Size | Field           | Description                                              |
|--------|------|-----------------|----------------------------------------------------------|
| 0      | 2    | `magic`         | Must equal `0x4D53` (ASCII "MS", little-endian)          |
| 2      | 1    | `version_major` | Protocol major version                                   |
| 3      | 1    | `version_minor` | Protocol minor version                                   |
| 4      | 1    | `msg_type`      | Message type (see table below)                           |
| 5      | 1    | `seq`           | Sequence number – echoed verbatim in the response        |
| 6      | 2    | `length`        | Byte count of the payload that follows the header        |

The payload immediately follows the header with no padding.
Maximum accepted payload length: **512 bytes**.

---

## Message Types

| Value  | Constant                     | Direction       | Description            |
|--------|------------------------------|-----------------|------------------------|
| `0x01` | `PICOMSO_MSG_GET_INFO`       | host → device   | Request firmware info  |
| `0x02` | `PICOMSO_MSG_GET_CAPABILITIES` | host → device | Request capability map |
| `0x03` | `PICOMSO_MSG_GET_STATUS`     | host → device   | Request device status  |
| `0x04` | `PICOMSO_MSG_SET_MODE`       | host → device   | Set operating mode     |
| `0x80` | `PICOMSO_MSG_ACK`            | device → host   | Successful response    |
| `0x81` | `PICOMSO_MSG_ERROR`          | device → host   | Error response         |

---

## Status / Error Codes

| Value  | Constant                        | Meaning                              |
|--------|---------------------------------|--------------------------------------|
| `0x00` | `PICOMSO_STATUS_OK`             | Success                              |
| `0x01` | `PICOMSO_STATUS_ERR_UNKNOWN`    | Unrecognised command                 |
| `0x02` | `PICOMSO_STATUS_ERR_BAD_MAGIC`  | Magic bytes mismatch                 |
| `0x03` | `PICOMSO_STATUS_ERR_BAD_LEN`    | Payload length out of range          |
| `0x04` | `PICOMSO_STATUS_ERR_BAD_MODE`   | Unknown mode value in `SET_MODE`     |
| `0x05` | `PICOMSO_STATUS_ERR_VERSION`    | Incompatible protocol major version  |

---

## ACK Response Payload

Used when the device responds successfully.  `msg_type` = `0x80`.

| Offset | Size | Field    | Description                  |
|--------|------|----------|------------------------------|
| 0      | 1    | `status` | Always `0x00` (`STATUS_OK`)  |

---

## ERROR Response Payload

Used when the device encounters an error.  `msg_type` = `0x81`.

| Offset    | Size      | Field     | Description                              |
|-----------|-----------|-----------|------------------------------------------|
| 0         | 1         | `status`  | `picomso_status_t` error code (non-zero) |
| 1         | 1         | `msg_len` | Byte length of the human-readable string |
| 2         | `msg_len` | `message` | UTF-8 text, **no** NUL terminator on wire |

---

## Command Definitions

### GET_INFO (`0x01`)

**Request payload:** none (`header.length == 0`)

**Response payload** (`PICOMSO_MSG_ACK`):

| Offset | Size | Field                   | Description                              |
|--------|------|-------------------------|------------------------------------------|
| 0      | 1    | `protocol_version_major`| Protocol major version the device speaks |
| 1      | 1    | `protocol_version_minor`| Protocol minor version                   |
| 2      | 32   | `fw_id`                 | NUL-terminated ASCII firmware identifier |

---

### GET_CAPABILITIES (`0x02`)

**Request payload:** none (`header.length == 0`)

**Response payload** (`PICOMSO_MSG_ACK`):

| Offset | Size | Field          | Description                        |
|--------|------|----------------|------------------------------------|
| 0      | 4    | `capabilities` | 32-bit little-endian capability map|

**Capability bits:**

| Bit | Constant            | Meaning                               |
|-----|---------------------|---------------------------------------|
| 0   | `PICOMSO_CAP_LOGIC` | Device supports logic-analyser mode   |
| 1   | `PICOMSO_CAP_SCOPE` | Device supports oscilloscope mode     |
| 2–31| (reserved)          | Must be zero                          |

---

### GET_STATUS (`0x03`)

**Request payload:** none (`header.length == 0`)

**Response payload** (`PICOMSO_MSG_ACK`):

| Offset | Size | Field           | Description                                   |
|--------|------|-----------------|-----------------------------------------------|
| 0      | 1    | `mode`          | Current operating mode (see values below)     |
| 1      | 1    | `capture_state` | Current capture state (see values below)      |

**Mode values:**

| Value  | Meaning                      |
|--------|------------------------------|
| `0x00` | No mode selected (`UNSET`)   |
| `0x01` | Logic-analyser mode          |
| `0x02` | Oscilloscope mode            |

**Capture state values:**

| Value  | Meaning                          |
|--------|----------------------------------|
| `0x00` | No capture in progress (`IDLE`)  |
| `0x01` | Capture active (`RUNNING`)       |

---

### SET_MODE (`0x04`)

**Request payload:**

| Offset | Size | Field  | Description                             |
|--------|------|--------|-----------------------------------------|
| 0      | 1    | `mode` | Desired mode (`0x00`, `0x01`, `0x02`)   |

**Response:** `ACK` on success, or `ERROR` with `PICOMSO_STATUS_ERR_BAD_MODE`
if the mode value is not one of the defined values.

**Effect on capture_controller:** A successful `SET_MODE` calls
`capture_controller_set_mode()` on the protocol layer's internal
`capture_controller_t` instance.  The new mode is immediately visible to
subsequent `GET_STATUS` requests.  The command does **not** start or stop
any capture hardware; real capture initiation is deferred to a future phase.

---

## Current Limitations

The protocol implementation is a **Phase 0** integration.  The following
constraints apply until a full transport adapter and hardware back-end are
wired in:

- **No real capture start/stop.**  `SET_MODE` updates the mode tracked by
  `capture_controller_t` only.  No ADC, PIO, or DMA operation is triggered.
- **Capture state is always `IDLE`.**  The `capture_state` field in
  `GET_STATUS` responses reflects `capture_controller_t.state`, which
  remains `CAPTURE_IDLE` because nothing in this phase sets it to
  `CAPTURE_RUNNING`.
- **Static capabilities.**  `GET_CAPABILITIES` always returns
  `PICOMSO_CAP_LOGIC | PICOMSO_CAP_SCOPE` regardless of the connected
  hardware.
- **Static firmware identifier.**  `GET_INFO` always returns `"PicoMSO-0.1"`.
- **Transport is dummy/mock only.**  The integration layer (`firmware/integration/`)
  connects the protocol and transport layers using an in-memory dummy backend
  (`dummy_transport_iface`).  No USB CDC adapter, no USB bulk adapter, and no
  UART adapter have been implemented in Phase 0.  Real wire transport remains
  a future task.

---

## Transport Independence

The protocol layer (`firmware/protocol/`) contains **no** transport-specific
code.  It operates on raw byte buffers:

- **Input:** a `const uint8_t *` buffer containing exactly one packet
  (header + payload).
- **Output:** a `picomso_response_t` buffer into which the full response
  packet (header + payload) is written.

A separate transport abstraction layer (`firmware/transport/`) provides the
`transport_interface_t` / `transport_ctx_t` types and the helper functions
`transport_send()` / `transport_receive()`.  The protocol layer does **not**
depend on `firmware/transport/`; both layers are independent and are
connected only through `firmware/integration/`.

## End-to-End Flow (Phase 0)

The full path a request takes in Phase 0:

```
Host packet → dummy_transport_iface.receive()
           → integration_process_one()
           → picomso_dispatch()
           → per-command handler (reads/writes capture_controller_t)
           → picomso_response_t filled
           → dummy_transport_iface.send()
           → response bytes in dummy tx_buf
```

The `integration_process_one()` function in `firmware/integration/src/integration.c`
is the single point that performs all three steps (receive → dispatch → send).

The caller is responsible for:
1. Pre-loading the dummy transport's receive buffer via
   `dummy_transport_set_rx()`.
2. Calling `integration_process_one()`.
3. Reading the response via `dummy_transport_get_tx()`.

**No real wire transport exists in Phase 0.**  The transport backend is
intentionally limited to the dummy/mock implementation.  Real hardware
transport (USB CDC, USB bulk, UART) is deferred to a future phase.
