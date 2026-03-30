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
 * PicoMSO unified protocol – per-command handler implementations.
 *
 * Each function in this file handles one message type.  Handlers build
 * a response packet into the supplied picomso_response_t buffer and
 * return the appropriate picomso_status_t.
 *
 * GET_STATUS and SET_MODE delegate to a module-level capture_controller_t
 * instance so that the protocol layer reflects real capture mode/state.
 * GET_INFO and GET_CAPABILITIES continue to return static values.
 *
 * The handlers remain transport-agnostic. The only concrete capture dependency
 * is the logic-analyzer backend used by READ_DATA_BLOCK.
 */

#include "protocol.h"
#include "protocol_packets.h"
#include "capture_controller.h"
#include "logic_capture.h"

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
 * The protocol layer owns the control-plane state store and delegates the
 * concrete logic-analyzer data path to firmware/mixed_signal/logic_capture.c.
 *
 * Starts in CAPTURE_MODE_UNSET / CAPTURE_IDLE.
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

    if (capture_controller_get_mode(&s_capture_ctrl) == CAPTURE_MODE_LOGIC) {
        capture_controller_set_state(&s_capture_ctrl, logic_capture_get_state());
    }

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
            logic_capture_reset();
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
            capture_controller_set_mode(&s_capture_ctrl, (capture_mode_t)req.mode);
            picomso_write_ack(hdr->seq, resp);
            return PICOMSO_STATUS_OK;

        case PICOMSO_MODE_LOGIC:
            logic_capture_arm();
            capture_controller_set_state(&s_capture_ctrl, logic_capture_get_state());
            capture_controller_set_mode(&s_capture_ctrl, (capture_mode_t)req.mode);
            picomso_write_ack(hdr->seq, resp);
            return PICOMSO_STATUS_OK;

        case PICOMSO_MODE_OSCILLOSCOPE:
            logic_capture_reset();
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
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
 * Returns one finalized logic-analyzer data block via PICOMSO_MSG_DATA_BLOCK.
 *
 * The block is captured lazily from GPIO 0..15 when the logic mode has been
 * armed via SET_MODE(LOGIC): 16 pre-trigger samples are held in a circular
 * ring, a rising edge on GPIO 0 finalizes the trigger point, and 15 more
 * post-trigger samples are appended for a one-shot 64-byte upload.
 *
 * Thread-safety: this handler, like all handlers in this file, assumes a
 * single-threaded polling context (the main loop in usb_control_plane/main.c).
 * No locking is applied.
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_read_data_block(const picomso_packet_header_t *hdr,
                                                const uint8_t                 *payload,
                                                picomso_response_t            *resp)
{
    (void)payload; /* READ_DATA_BLOCK carries no request payload */
    if (capture_controller_get_mode(&s_capture_ctrl) != CAPTURE_MODE_LOGIC) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE,
                            "logic mode not active", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    picomso_data_block_response_t blk;
    if (!logic_capture_read_block(&blk.block_id, blk.data, &blk.data_len)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN,
                            "logic capture not armed", resp);
        return PICOMSO_STATUS_ERR_UNKNOWN;
    }

    capture_controller_set_state(&s_capture_ctrl, logic_capture_get_state());

    build_response(hdr, PICOMSO_MSG_DATA_BLOCK,
                   &blk, (uint16_t)sizeof(blk), resp);
    return PICOMSO_STATUS_OK;
}
