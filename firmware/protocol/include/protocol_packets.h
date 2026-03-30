/*
 * PicoMSO - RP2040 Mixed Signal Oscilloscope
 * Copyright (C) 2024 Daniel Gorbea <danielgorbea@hotmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * PicoMSO unified protocol – per-command packet structures (Phase 0).
 *
 * Each command supported by this phase is defined here as a pair of
 * packed C structs: one for the request payload and one for the response
 * payload.  The outermost framing (picomso_packet_header_t) is defined in
 * protocol.h and is not repeated here.
 *
 * All multi-byte fields are little-endian unless noted otherwise.
 */

#ifndef PICOMSO_PROTOCOL_PACKETS_H
#define PICOMSO_PROTOCOL_PACKETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "protocol.h"

/* -----------------------------------------------------------------------
 * GET_INFO  (PICOMSO_MSG_GET_INFO = 0x01)
 *
 * Request:  No payload (header.length == 0).
 *
 * Response: picomso_info_response_t
 *   Returns a human-readable firmware identifier string, the protocol
 *   version the device speaks, and a device capability bitmap (see
 *   GET_CAPABILITIES for bit definitions).
 * ----------------------------------------------------------------------- */

/** Maximum length of the firmware identifier string (including NUL). */
#define PICOMSO_INFO_FW_ID_MAX  32u

typedef struct {
    uint8_t  protocol_version_major;
    uint8_t  protocol_version_minor;
    char     fw_id[PICOMSO_INFO_FW_ID_MAX]; /**< NUL-terminated ASCII string */
} __attribute__((packed)) picomso_info_response_t;

/* -----------------------------------------------------------------------
 * GET_CAPABILITIES  (PICOMSO_MSG_GET_CAPABILITIES = 0x02)
 *
 * Request:  No payload (header.length == 0).
 *
 * Response: picomso_capabilities_response_t
 *   A 32-bit capability bitmap.  Bit assignments:
 *     bit 0  – PICOMSO_CAP_LOGIC   : device supports logic-analyser mode
 *     bit 1  – PICOMSO_CAP_SCOPE   : device supports oscilloscope mode
 *     bits 2–31 reserved, must be zero.
 * ----------------------------------------------------------------------- */

#define PICOMSO_CAP_LOGIC  UINT32_C(1 << 0)
#define PICOMSO_CAP_SCOPE  UINT32_C(1 << 1)

typedef struct {
    uint32_t capabilities; /**< Capability bitmap (see PICOMSO_CAP_* above) */
} __attribute__((packed)) picomso_capabilities_response_t;

/* -----------------------------------------------------------------------
 * GET_STATUS  (PICOMSO_MSG_GET_STATUS = 0x03)
 *
 * Request:  No payload (header.length == 0).
 *
 * Response: picomso_status_response_t
 *   Returns the current operating mode and capture state.
 *
 *   mode values:
 *     0x00 – PICOMSO_MODE_UNSET        (no mode selected)
 *     0x01 – PICOMSO_MODE_LOGIC        (logic-analyser back-end active)
 *     0x02 – PICOMSO_MODE_OSCILLOSCOPE (oscilloscope back-end active)
 *
 *   capture_state values:
 *     0x00 – PICOMSO_CAPTURE_IDLE    (no capture in progress)
 *     0x01 – PICOMSO_CAPTURE_RUNNING (capture active)
 * ----------------------------------------------------------------------- */

typedef enum {
    PICOMSO_MODE_UNSET        = 0x00,
    PICOMSO_MODE_LOGIC        = 0x01,
    PICOMSO_MODE_OSCILLOSCOPE = 0x02,
} picomso_device_mode_t;

typedef enum {
    PICOMSO_CAPTURE_IDLE    = 0x00,
    PICOMSO_CAPTURE_RUNNING = 0x01,
} picomso_capture_state_t;

typedef struct {
    uint8_t mode;          /**< picomso_device_mode_t    */
    uint8_t capture_state; /**< picomso_capture_state_t  */
} __attribute__((packed)) picomso_status_response_t;

/* -----------------------------------------------------------------------
 * SET_MODE  (PICOMSO_MSG_SET_MODE = 0x04)
 *
 * Request:  picomso_set_mode_request_t
 *   Instructs the device to switch to the specified operating mode.
 *   The device must not be in CAPTURE_RUNNING state when this command
 *   is issued; if it is, the device returns PICOMSO_STATUS_ERR_BAD_MODE.
 *
 *   mode values: same as picomso_device_mode_t above.
 *
 * Response: ACK packet (no additional payload) on success, or ERROR packet
 *   with PICOMSO_STATUS_ERR_BAD_MODE if the mode value is unknown.
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t mode; /**< picomso_device_mode_t */
} __attribute__((packed)) picomso_set_mode_request_t;

/* -----------------------------------------------------------------------
 * REQUEST_CAPTURE  (PICOMSO_MSG_REQUEST_CAPTURE = 0x05)
 *
 * Request:  picomso_request_capture_request_t
 *   Starts one full logic-analyzer capture. The device performs the complete
 *   one-shot acquisition before acknowledging the command. The completed
 *   capture remains stored for later READ_DATA_BLOCK requests.
 *
 * Response: ACK packet (no additional payload) on success.
 * ----------------------------------------------------------------------- */

typedef struct {
    uint32_t total_samples;       /**< Full requested capture length in samples */
    uint32_t pre_trigger_samples; /**< Requested pre-trigger sample count        */
} __attribute__((packed)) picomso_request_capture_request_t;

/* -----------------------------------------------------------------------
 * READ_DATA_BLOCK  (PICOMSO_MSG_READ_DATA_BLOCK = 0x06)
 *
 * Request:  No payload (header.length == 0).
 *   Asks the device to return one fixed-size chunk from the completed capture
 *   buffer. Acquisition must already be finished before readout begins.
 *
 * Response: picomso_data_block_response_t  (msg_type = PICOMSO_MSG_DATA_BLOCK)
 *   The response is delivered over the BULK IN endpoint (EP6_IN).  The
 *   control plane remains on EP0; this response is the first data-plane
 *   packet in the PicoMSO protocol.
 *
 *   READ_DATA_BLOCK does not expose live acquisition data.
 *   It only serves bytes from a finalized stored capture.
 * ----------------------------------------------------------------------- */

/**
 * READ_DATA_BLOCK carries no request payload.
 * The host simply sends a header with msg_type = PICOMSO_MSG_READ_DATA_BLOCK
 * and header.length = 0 to receive the next chunk from the stored capture.
 */

/**
 * DATA_BLOCK response payload.
 *
 * Offset   Size        Field       Description
 *   0        2         block_id    Monotonically incrementing block counter.
 *   2        2         data_len    Byte count of the following sample data.
 *   4      data_len    data        Raw sample bytes.
 *
 * The full response wire format is:
 *   picomso_packet_header_t  (8 bytes, msg_type = PICOMSO_MSG_DATA_BLOCK)
 *   picomso_data_block_response_t
 */
typedef struct {
    uint16_t block_id; /**< Monotonically incrementing block counter       */
    uint16_t data_len; /**< Byte count of the data[] field that follows    */
    uint8_t  data[PICOMSO_DATA_BLOCK_SIZE]; /**< Sample bytes              */
} __attribute__((packed)) picomso_data_block_response_t;

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_PROTOCOL_PACKETS_H */
