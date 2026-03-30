# PicoMSO Protocol Specification (Phase 1)

This document describes the transport-agnostic protocol layer introduced in
`firmware/protocol/`.  Phase 1 extends Phase 0 with the first minimal
data-plane command (`READ_DATA_BLOCK`) that exercises the BULK IN path with a
dummy sample payload.

---

## Scope and Intentional Limitations

This phase establishes the **wire format**, **command vocabulary**,
**control-plane state management**, and a **minimal data-plane path** for the
future unified PicoMSO host protocol.  It does **not**:

- Replace the existing SUMP protocol used by `logic_analyzer_rp2040`.
- Replace the existing custom USB binary protocol used by `oscilloscope_rp2040`.
- Introduce any USB, CDC, bulk-endpoint, PIO, ADC, or DMA dependency in the
  protocol layer itself.
- Implement real logic or analog capture.
- Implement DMA / ADC / PIO integration.
- Implement streaming (one block per explicit request only).
- Change any current firmware behaviour.

Both imported firmware projects remain independently buildable and unchanged.

---

## Versioning

| Constant                            | Value | Meaning                                         |
|-------------------------------------|-------|-------------------------------------------------|
| `PICOMSO_PROTOCOL_VERSION_MAJOR`    | `0`   | Bump on incompatible wire-format change         |
| `PICOMSO_PROTOCOL_VERSION_MINOR`    | `2`   | Bump when new commands are added                |

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
| `0x05` | `PICOMSO_MSG_READ_DATA_BLOCK`  | host → device   | Request one data-plane block      |
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
`capture_controller_t` instance. In logic mode it also arms the concrete
logic-capture backend, so subsequent `GET_STATUS` requests report
`CAPTURE_RUNNING` until one capture is finalized and uploaded.

---

### READ_DATA_BLOCK (`0x05`)

**Request payload:** none (`header.length == 0`)

**Response:** `picomso_data_block_response_t` with `msg_type = 0x82`
(`PICOMSO_MSG_DATA_BLOCK`), delivered over the **BULK IN** endpoint (EP6_IN).

**Response payload:**

| Offset      | Size        | Field      | Description                                  |
|-------------|-------------|------------|----------------------------------------------|
| 0           | 1           | `block_id` | Monotonically incrementing capture block counter |
| 1           | 2           | `data_len` | Byte count of the data bytes that follow         |
| 3           | `data_len`  | `data`     | Raw GPIO sample bytes                            |

**Logic payload:** In logic mode the device returns one finalized 64-byte
one-shot capture block. The capture is armed by `SET_MODE(LOGIC)`, stores
16 pre-trigger samples in a circular ring, triggers on a rising edge on
GPIO 0, then appends 15 post-trigger samples. Each sample is a little-endian
16-bit snapshot of GPIO 0..15.

**Transport note:**  The request arrives as a vendor OUT control transfer on
EP0.  The DATA_BLOCK response is sent over EP6 IN (BULK IN), establishing the
first split between control-plane (EP0) and data-plane (BULK IN).  No BULK OUT
endpoint is added; descriptors and endpoint configuration are unchanged.

---

## Current Limitations

The protocol implementation handles all defined commands through both the dummy
transport (testing) and the real USB transport backend
(`firmware/transport/usb/`).  The following constraints still apply:

- **Logic capture only.** `READ_DATA_BLOCK` returns one real 64-byte logic
  capture block from GPIO 0..15. Oscilloscope capture is still out of scope.
- **No streaming.**  One block is returned per explicit `READ_DATA_BLOCK`
  request.  Continuous streaming is out of scope.
- **Logic re-arm is explicit.** `SET_MODE(LOGIC)` arms a new capture. Repeated
  `READ_DATA_BLOCK` requests return the same finalized block until logic mode
  is armed again.
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

## End-to-End Flow (USB backend – READ_DATA_BLOCK)

The first data-plane path.  The request still arrives on EP0; the response
(carrying sample data) is returned over the existing BULK IN endpoint (EP6_IN).
No new endpoints, descriptors, or hardware are added.

```
Host (vendor OUT control transfer on EP0, msg_type = 0x05)
           → control_transfer_handler()
           → static rx_buf in usb_transport.c
           → integration_process_one()
           → transport_receive()
           → picomso_dispatch()
            → picomso_handle_read_data_block()
                → logic_capture_read_block()
                  → pre-trigger ring on GPIO 0..15
                  → trigger detect on GPIO 0 rising edge
                  → finite post-trigger capture
                  → finalized 64-byte block
            → picomso_response_t filled      [DATA_BLOCK, msg_type = 0x82]
            → transport_send()              [usb_transport_iface.send]
            → EP6 IN bulk transfer
Host ←   (receives DATA_BLOCK response with 64 bytes of GPIO samples)
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
           → per-command handler (reads/writes capture_controller_t, or
                                  builds data block for READ_DATA_BLOCK)
           → picomso_response_t filled
           → dummy_transport_iface.send()
           → response bytes in dummy tx_buf
```

The caller is responsible for:
1. Pre-loading the dummy transport's receive buffer via
   `dummy_transport_set_rx()`.
2. Calling `integration_process_one()`.
3. Reading the response via `dummy_transport_get_tx()`.
