# PicoMSO Protocol Specification (Phase 1)

This document describes the transport-agnostic protocol layer introduced in
`firmware/protocol/`. Phase 1 now includes concrete one-shot
request/complete/readout flows for both logic-analyzer and oscilloscope mode:
`REQUEST_CAPTURE` performs the full one-shot capture, and `READ_DATA_BLOCK`
returns fixed-size chunks from the finalized stored capture buffer.

---

## Scope and Intentional Limitations

This phase establishes the **wire format**, **command vocabulary**,
**control-plane state management**, and a **minimal data-plane path** for the
future unified PicoMSO host protocol.  It does **not**:

- Replace the existing SUMP protocol used by `logic_analyzer_rp2040`.
- Replace the existing custom USB binary protocol used by `oscilloscope_rp2040`.
- Introduce any USB, CDC, bulk-endpoint, PIO, ADC, or DMA dependency in the
  protocol layer itself.
- Implement continuous oscilloscope streaming.
- Implement DMA-based oscilloscope acquisition or PIO-based mixed-signal capture.
- Implement live streaming during acquisition.
- Change any current firmware behaviour.

Both imported firmware projects remain independently buildable and unchanged.

---

## Versioning

| Constant                            | Value | Meaning                                         |
|-------------------------------------|-------|-------------------------------------------------|
| `PICOMSO_PROTOCOL_VERSION_MAJOR`    | `0`   | Bump on incompatible wire-format change         |
| `PICOMSO_PROTOCOL_VERSION_MINOR`    | `3`   | Bump when new commands are added                |

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

| Value  | Constant                       | Direction       | Description                       |
|--------|--------------------------------|-----------------|-----------------------------------|
| `0x01` | `PICOMSO_MSG_GET_INFO`         | host → device   | Request firmware info             |
| `0x02` | `PICOMSO_MSG_GET_CAPABILITIES` | host → device   | Request capability map            |
| `0x03` | `PICOMSO_MSG_GET_STATUS`       | host → device   | Request device status             |
| `0x04` | `PICOMSO_MSG_SET_MODE`         | host → device   | Set operating mode                |
| `0x05` | `PICOMSO_MSG_REQUEST_CAPTURE`  | host → device   | Perform one full one-shot capture |
| `0x06` | `PICOMSO_MSG_READ_DATA_BLOCK`  | host → device   | Read one finalized capture chunk  |
| `0x80` | `PICOMSO_MSG_ACK`              | device → host   | Successful control-plane response |
| `0x81` | `PICOMSO_MSG_ERROR`            | device → host   | Error response                    |
| `0x82` | `PICOMSO_MSG_DATA_BLOCK`       | device → host   | Data-plane block response         |

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
`capture_controller_t` instance and resets any previously stored logic capture.

---

### REQUEST_CAPTURE (`0x05`)

**Request payload:**

| Offset | Size | Field                 | Description                               |
|--------|------|-----------------------|-------------------------------------------|
| 0      | 4    | `total_samples`       | Full requested capture length in samples  |
| 4      | 4    | `pre_trigger_samples` | Requested pre-trigger sample count        |

**Response:** `ACK` on success.

**Semantics:** This command is the capture request. The device performs a
finite one-shot capture in the active mode, stores the completed result, and
only then acknowledges the request.

In logic mode the device:

1. starts a one-shot logic capture
2. retains pre-trigger samples in a circular buffer
3. detects the trigger
4. collects the remaining post-trigger samples needed to satisfy the full
   requested total capture length
5. finalizes and stores the completed capture
6. only then acknowledges the request

In oscilloscope mode the device currently performs the same finite
request-sized acquisition model using ADC input 0 (GPIO 26), stores raw
little-endian 16-bit ADC samples, and exposes them only after completion
through repeated `READ_DATA_BLOCK` requests. `pre_trigger_samples` is accepted
for protocol compatibility but does not yet enable a separate analog-trigger
algorithm.

After the `ACK`, the host reads the stored completed capture through repeated
`READ_DATA_BLOCK` requests.

---

### READ_DATA_BLOCK (`0x06`)

**Request payload:** none (`header.length == 0`)

**Response:** `picomso_data_block_response_t` with `msg_type = 0x82`
(`PICOMSO_MSG_DATA_BLOCK`), delivered over the **BULK IN** endpoint (EP6_IN).

**Response payload:**

| Offset      | Size        | Field      | Description                                      |
|-------------|-------------|------------|--------------------------------------------------|
| 0           | 2           | `block_id` | Monotonically incrementing finalized readout chunk |
| 2           | 2           | `data_len` | Byte count of the data bytes that follow         |
| 4           | `data_len`  | `data`     | Raw GPIO sample bytes                            |

**Logic payload:** In logic mode the device returns the next fixed-size chunk
from the completed stored capture buffer. The transport chunk size remains
fixed at 64 bytes, but the total capture length is request-defined by the
preceding `REQUEST_CAPTURE` command. Each logic sample is a little-endian
16-bit snapshot of GPIO 0..15.

**Oscilloscope payload:** In oscilloscope mode the device returns the next
fixed-size chunk from the completed stored capture buffer. Each sample is a
little-endian 16-bit raw ADC conversion from ADC input 0 (GPIO 26). The
transport chunk size remains fixed at 64 bytes and no live acquisition data is
exposed during capture.

**Transport note:**  The request arrives as a vendor OUT control transfer on
EP0.  The DATA_BLOCK response is sent over EP6 IN (BULK IN), establishing the
first split between control-plane (EP0) and data-plane (BULK IN).  No BULK OUT
endpoint is added; descriptors and endpoint configuration are unchanged.

---

## Current Limitations

The protocol implementation handles all defined commands through both the dummy
transport (testing) and the real USB transport backend
(`firmware/transport/usb/`).  The following constraints still apply:

- **No streaming.** Acquisition completes before any readout begins.
- **Fixed transport chunk, variable capture length.** `READ_DATA_BLOCK` returns
  fixed-size chunks, but the full capture size comes from `REQUEST_CAPTURE`.
- **No oscilloscope-specific trigger model yet.** Scope mode accepts the same
  request shape, but does not yet implement a separate analog trigger path.
- **Static capabilities.**  `GET_CAPABILITIES` always returns
  `PICOMSO_CAP_LOGIC | PICOMSO_CAP_SCOPE` regardless of the connected
  hardware.
- **Static firmware identifier.**  `GET_INFO` always returns `"PicoMSO-0.1"`.

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

## End-to-End Flow (USB backend – control-plane commands)

The full path a control-plane request takes through the real USB transport backend:

```
Host (vendor OUT control transfer on EP0)
           → control_transfer_handler()     [USB driver IRQ callback]
           → static rx_buf in usb_transport.c
           → integration_process_one()
           → transport_receive()            [usb_transport_iface.receive]
           → picomso_dispatch()
           → per-command handler (reads/writes capture_controller_t)
           → picomso_response_t filled      [ACK or ERROR]
           → transport_send()              [usb_transport_iface.send]
           → EP6 IN bulk transfer
Host ←
```

## End-to-End Flow (USB backend – REQUEST_CAPTURE + READ_DATA_BLOCK)

The capture request and the later readout requests still arrive on EP0; the
data-carrying response is returned over the existing BULK IN endpoint (EP6_IN).
No new endpoints, descriptors, or hardware are added.

```
Host (vendor OUT control transfer on EP0, msg_type = 0x05)
            → control_transfer_handler()
            → static rx_buf in usb_transport.c
            → integration_process_one()
            → transport_receive()
            → picomso_dispatch()
             → picomso_handle_request_capture()
                 → logic_capture_start()
                   → request-defined pre-trigger ring
                   → trigger detect on GPIO 0 rising edge
                   → finite post-trigger capture until total_samples is complete
                   → finalized stored capture buffer
             → ACK

Host (later vendor OUT control transfer on EP0, msg_type = 0x06)
            → control_transfer_handler()
            → static rx_buf in usb_transport.c
            → integration_process_one()
            → transport_receive()
            → picomso_dispatch()
             → picomso_handle_read_data_block()
                 → logic_capture_read_block()
                   → next fixed-size chunk from finalized capture buffer
             → picomso_response_t filled      [DATA_BLOCK, msg_type = 0x82]
             → transport_send()              [usb_transport_iface.send]
             → EP6 IN bulk transfer
Host ←   (receives DATA_BLOCK response with the next capture chunk)
```

See `firmware/examples/usb_control_plane/main.c` for the complete annotated
entry point that wires these layers together.

## End-to-End Flow (Dummy backend)

The dummy transport path remains available for testing and architecture
validation without hardware:

```
Host packet → dummy_transport_iface.receive()
           → integration_process_one()
           → picomso_dispatch()
           → per-command handler (reads/writes capture_controller_t,
                                  performs REQUEST_CAPTURE, or
                                  reads finalized data for READ_DATA_BLOCK)
           → picomso_response_t filled
           → dummy_transport_iface.send()
           → response bytes in dummy tx_buf
```

The caller is responsible for:
1. Pre-loading the dummy transport's receive buffer via
   `dummy_transport_set_rx()`.
2. Calling `integration_process_one()`.
3. Reading the response via `dummy_transport_get_tx()`.
