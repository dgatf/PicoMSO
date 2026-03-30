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
 * PicoMSO unified protocol – per-command handler implementations (Phase 0).
 *
 * Each function in this file handles one message type.  Handlers build
 * a response packet into the supplied picomso_response_t buffer and
 * return the appropriate picomso_status_t.
 *
 * GET_STATUS and SET_MODE delegate to a module-level capture_controller_t
 * instance so that the protocol layer reflects real capture mode/state.
 * GET_INFO and GET_CAPABILITIES continue to return static values.
 *
 * No dependency on any transport (USB, UART, …), PIO, ADC, or DMA.
 */

#include "protocol.h"
#include "protocol_packets.h"
#include "capture_controller.h"

#include <string.h>

/* -----------------------------------------------------------------------
 * Firmware identification string returned by GET_INFO.
 * Update this when the firmware gains a meaningful version tag.
 * ----------------------------------------------------------------------- */

#define PICOMSO_FW_ID  "PicoMSO-0.1"

/* -----------------------------------------------------------------------
 * Static device capability bitmap.
 * In a future phase this may be read from hardware-detection logic.
 * ----------------------------------------------------------------------- */

#define PICOMSO_STATIC_CAPABILITIES  (PICOMSO_CAP_LOGIC | PICOMSO_CAP_SCOPE)

/* -----------------------------------------------------------------------
 * Module-level capture controller instance.
 *
 * The protocol layer owns control-plane state only.  Starts in
 * CAPTURE_MODE_UNSET / CAPTURE_IDLE.  No hardware is accessed;
 * SET_MODE and GET_STATUS operate on this struct alone.
 * ----------------------------------------------------------------------- */

static capture_controller_t s_capture_ctrl = {
    .mode  = CAPTURE_MODE_UNSET,
    .state = CAPTURE_IDLE
};

/* -----------------------------------------------------------------------
 * Internal helper: build a full response packet.
 *
 * Copies hdr.seq into the response, sets msg_type to resp_type, and
 * appends payload_len bytes from payload.
 * ----------------------------------------------------------------------- */

static void build_response(const picomso_packet_header_t *req_hdr,
                            picomso_msg_type_t             resp_type,
                            const void                    *payload,
                            uint16_t                       payload_len,
                            picomso_response_t            *resp)
{
    picomso_packet_header_t out_hdr;
    out_hdr.magic         = PICOMSO_PACKET_MAGIC;
    out_hdr.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    out_hdr.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    out_hdr.msg_type      = (uint8_t)resp_type;
    out_hdr.seq           = req_hdr->seq;
    out_hdr.length        = payload_len;

    size_t total = sizeof(out_hdr) + payload_len;
    if (total > sizeof(resp->buf)) {
        resp->used = 0;
        return;
    }

    memcpy(resp->buf, &out_hdr, sizeof(out_hdr));
    if (payload != NULL && payload_len > 0) {
        memcpy(resp->buf + sizeof(out_hdr), payload, payload_len);
    }
    resp->used = total;
}

/* -----------------------------------------------------------------------
 * GET_INFO handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_info(const picomso_packet_header_t *hdr,
                                         const uint8_t                 *payload,
                                         picomso_response_t            *resp)
{
    (void)payload; /* GET_INFO carries no request payload */

    picomso_info_response_t info;
    info.protocol_version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    info.protocol_version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;

    /* Zero the string field, then copy the identifier (NUL-safe). */
    memset(info.fw_id, 0, sizeof(info.fw_id));
    strncpy(info.fw_id, PICOMSO_FW_ID, sizeof(info.fw_id) - 1u);

    build_response(hdr, PICOMSO_MSG_ACK,
                   &info, (uint16_t)sizeof(info), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * GET_CAPABILITIES handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_capabilities(const picomso_packet_header_t *hdr,
                                                 const uint8_t                 *payload,
                                                 picomso_response_t            *resp)
{
    (void)payload; /* GET_CAPABILITIES carries no request payload */

    picomso_capabilities_response_t caps;
    caps.capabilities = PICOMSO_STATIC_CAPABILITIES;

    build_response(hdr, PICOMSO_MSG_ACK,
                   &caps, (uint16_t)sizeof(caps), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * GET_STATUS handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_status(const picomso_packet_header_t *hdr,
                                           const uint8_t                 *payload,
                                           picomso_response_t            *resp)
{
    (void)payload; /* GET_STATUS carries no request payload */

    picomso_status_response_t status;
    status.mode          = (uint8_t)capture_controller_get_mode(&s_capture_ctrl);
    status.capture_state = (uint8_t)capture_controller_get_state(&s_capture_ctrl);

    build_response(hdr, PICOMSO_MSG_ACK,
                   &status, (uint16_t)sizeof(status), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * SET_MODE handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_set_mode(const picomso_packet_header_t *hdr,
                                         const uint8_t                 *payload,
                                         picomso_response_t            *resp)
{
    if (hdr->length < (uint16_t)sizeof(picomso_set_mode_request_t)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN,
                            "SET_MODE payload too short", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    picomso_set_mode_request_t req;
    memcpy(&req, payload, sizeof(req));

    switch ((picomso_device_mode_t)req.mode) {
        case PICOMSO_MODE_UNSET:
        case PICOMSO_MODE_LOGIC:
        case PICOMSO_MODE_OSCILLOSCOPE:
            capture_controller_set_mode(&s_capture_ctrl, (capture_mode_t)req.mode);
            picomso_write_ack(hdr->seq, resp);
            return PICOMSO_STATUS_OK;

        default:
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE,
                                "unknown mode", resp);
            return PICOMSO_STATUS_ERR_BAD_MODE;
    }
}

/* -----------------------------------------------------------------------
 * READ_DATA_BLOCK handler
 *
 * Returns one fixed dummy data block via PICOMSO_MSG_DATA_BLOCK.
 *
 * The payload is a 64-byte ramp pattern (bytes 0x00..0x3F) stored as a
 * compile-time constant array.  No real capture hardware (ADC, PIO, DMA)
 * is involved.  The block_id field increments with each successful call
 * so that the host can detect dropped or repeated blocks.
 *
 * Thread-safety: this handler, like all handlers in this file, assumes a
 * single-threaded polling context (the main loop in usb_control_plane/main.c).
 * No locking is applied.
 *
 * This is Phase 1 data-plane: dummy/sample payload only.
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_read_data_block(const picomso_packet_header_t *hdr,
                                                const uint8_t                 *payload,
                                                picomso_response_t            *resp)
{
    (void)payload; /* READ_DATA_BLOCK carries no request payload */

    /* Fixed ramp pattern: 0x00, 0x01, …, 0x3F.
     * Initialised once; copied into each response block. */
    static const uint8_t s_ramp[PICOMSO_DATA_BLOCK_SIZE] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    };

    static uint8_t s_block_id = 0u;

    picomso_data_block_response_t blk;
    blk.block_id = s_block_id++;
    blk.data_len = (uint16_t)PICOMSO_DATA_BLOCK_SIZE;
    memcpy(blk.data, s_ramp, PICOMSO_DATA_BLOCK_SIZE);

    build_response(hdr, PICOMSO_MSG_DATA_BLOCK,
                   &blk, (uint16_t)sizeof(blk), resp);
    return PICOMSO_STATUS_OK;
}
