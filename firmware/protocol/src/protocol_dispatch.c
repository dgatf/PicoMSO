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
 * GET_STATUS, SET_MODE, and REQUEST_CAPTURE delegate to a module-level capture_controller_t
 * instance so that the protocol layer reflects real capture mode/state.
 * GET_INFO and GET_CAPABILITIES continue to return static values.
 *
 * The handlers remain transport-agnostic. Concrete capture backends are
 * selected by the active mode for REQUEST_CAPTURE and READ_DATA_BLOCK.
 */

#include <string.h>

#include "capture_controller.h"
#include "logic_capture.h"
#include "protocol.h"
#include "protocol_packets.h"
#include "scope_capture.h"

/* -----------------------------------------------------------------------
 * Firmware identification string returned by GET_INFO.
 * Update this when the firmware gains a meaningful version tag.
 * ----------------------------------------------------------------------- */

#define PICOMSO_FW_ID "PicoMSO-0.1"
#define PICOMSO_LOGIC_TRIGGER_PIN_COUNT 16u

/* -----------------------------------------------------------------------
 * Static device capability bitmap.
 * In a future phase this may be read from hardware-detection logic.
 * ----------------------------------------------------------------------- */

#define PICOMSO_STATIC_CAPABILITIES (PICOMSO_CAP_LOGIC | PICOMSO_CAP_SCOPE)

/* -----------------------------------------------------------------------
 * Module-level capture controller instance.
 *
 * The protocol layer owns the control-plane state store and delegates the
 * concrete capture data paths to the mixed-signal backends.
 *
 * Starts in CAPTURE_MODE_UNSET / CAPTURE_IDLE.
 * ----------------------------------------------------------------------- */

static capture_controller_t s_capture_ctrl = {.mode = CAPTURE_MODE_UNSET, .state = CAPTURE_IDLE};

/* -----------------------------------------------------------------------
 * Internal helper: build a full response packet.
 *
 * Copies hdr.seq into the response, sets msg_type to resp_type, and
 * appends payload_len bytes from payload.
 * ----------------------------------------------------------------------- */

static void build_response(const picomso_packet_header_t *req_hdr, picomso_msg_type_t resp_type, const void *payload,
                           uint16_t payload_len, picomso_response_t *resp) {
    picomso_packet_header_t out_hdr;
    out_hdr.magic = PICOMSO_PACKET_MAGIC;
    out_hdr.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    out_hdr.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    out_hdr.msg_type = (uint8_t)resp_type;
    out_hdr.seq = req_hdr->seq;
    out_hdr.length = payload_len;

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

static void set_default_capture_triggers(capture_config_t *capture_config) {
    capture_config->trigger[0].is_enabled = true;
    capture_config->trigger[0].pin = 0u;
    capture_config->trigger[0].match = TRIGGER_TYPE_EDGE_HIGH;

    capture_config->trigger[1].is_enabled = false;
    capture_config->trigger[1].pin = 0u;
    capture_config->trigger[1].match = TRIGGER_TYPE_LEVEL_LOW;

    capture_config->trigger[2].is_enabled = false;
    capture_config->trigger[2].pin = 0u;
    capture_config->trigger[2].match = TRIGGER_TYPE_LEVEL_LOW;

    capture_config->trigger[3].is_enabled = false;
    capture_config->trigger[3].pin = 0u;
    capture_config->trigger[3].match = TRIGGER_TYPE_LEVEL_LOW;
}

static bool request_trigger_is_valid(const picomso_trigger_config_t *trigger) {
    if (trigger->is_enabled > 1u || trigger->pin >= PICOMSO_LOGIC_TRIGGER_PIN_COUNT) {
        return false;
    }

    switch ((picomso_trigger_match_t)trigger->match) {
        case PICOMSO_TRIGGER_MATCH_LEVEL_LOW:
        case PICOMSO_TRIGGER_MATCH_LEVEL_HIGH:
        case PICOMSO_TRIGGER_MATCH_EDGE_LOW:
        case PICOMSO_TRIGGER_MATCH_EDGE_HIGH:
            return true;
        default:
            return false;
    }
}

static void copy_request_triggers(capture_config_t *capture_config, const picomso_request_capture_request_t *req) {
    for (uint32_t i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
        capture_config->trigger[i].is_enabled = (req->trigger[i].is_enabled != 0u);
        capture_config->trigger[i].pin = req->trigger[i].pin;
        capture_config->trigger[i].match = (trigger_match_t)req->trigger[i].match;
    }
}

/* -----------------------------------------------------------------------
 * GET_INFO handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_info(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                         picomso_response_t *resp) {
    (void)payload; /* GET_INFO carries no request payload */

    picomso_info_response_t info;
    info.protocol_version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    info.protocol_version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;

    /* Zero the string field, then copy the identifier (NUL-safe). */
    memset(info.fw_id, 0, sizeof(info.fw_id));
    strncpy(info.fw_id, PICOMSO_FW_ID, sizeof(info.fw_id) - 1u);

    build_response(hdr, PICOMSO_MSG_ACK, &info, (uint16_t)sizeof(info), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * GET_CAPABILITIES handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_capabilities(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                                 picomso_response_t *resp) {
    (void)payload; /* GET_CAPABILITIES carries no request payload */

    picomso_capabilities_response_t caps;
    caps.capabilities = PICOMSO_STATIC_CAPABILITIES;

    build_response(hdr, PICOMSO_MSG_ACK, &caps, (uint16_t)sizeof(caps), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * GET_STATUS handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_status(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                           picomso_response_t *resp) {
    (void)payload; /* GET_STATUS carries no request payload */

    if (capture_controller_get_mode(&s_capture_ctrl) == CAPTURE_MODE_LOGIC) {
        capture_controller_set_state(&s_capture_ctrl, logic_capture_get_state());
    } else if (capture_controller_get_mode(&s_capture_ctrl) == CAPTURE_MODE_OSCILLOSCOPE) {
        capture_controller_set_state(&s_capture_ctrl, scope_capture_get_state());
    }

    picomso_status_response_t status;
    status.mode = (uint8_t)capture_controller_get_mode(&s_capture_ctrl);
    status.capture_state = (uint8_t)capture_controller_get_state(&s_capture_ctrl);

    build_response(hdr, PICOMSO_MSG_ACK, &status, (uint16_t)sizeof(status), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * SET_MODE handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_set_mode(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                         picomso_response_t *resp) {
    if (hdr->length < (uint16_t)sizeof(picomso_set_mode_request_t)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "SET_MODE payload too short", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    picomso_set_mode_request_t req;
    memcpy(&req, payload, sizeof(req));

    switch ((picomso_device_mode_t)req.mode) {
        case PICOMSO_MODE_UNSET:
            logic_capture_reset();
            scope_capture_reset();
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
            capture_controller_set_mode(&s_capture_ctrl, (capture_mode_t)req.mode);
            picomso_write_ack(hdr->seq, resp);
            return PICOMSO_STATUS_OK;

        case PICOMSO_MODE_LOGIC:
            logic_capture_reset();
            scope_capture_reset();
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
            capture_controller_set_mode(&s_capture_ctrl, (capture_mode_t)req.mode);
            picomso_write_ack(hdr->seq, resp);
            return PICOMSO_STATUS_OK;

        case PICOMSO_MODE_OSCILLOSCOPE:
            logic_capture_reset();
            scope_capture_reset();
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
            capture_controller_set_mode(&s_capture_ctrl, (capture_mode_t)req.mode);
            picomso_write_ack(hdr->seq, resp);
            return PICOMSO_STATUS_OK;

        default:
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE, "unknown mode", resp);
            return PICOMSO_STATUS_ERR_BAD_MODE;
    }
}

/* -----------------------------------------------------------------------
 * REQUEST_CAPTURE handler
 *
 * Starts a capture for the currently selected backend.
 *
 * For logic mode, capture start is asynchronous: this handler validates the
 * request, arms the capture backend, sets the controller state to RUNNING,
 * and returns ACK immediately. The backend completion callback later moves
 * the controller state back to IDLE when acquisition finishes.
 *
 * For scope mode, behavior currently depends on the backend implementation.
 * The completed capture data becomes available through later READ_DATA_BLOCK
 * calls once acquisition has finished.
 * ----------------------------------------------------------------------- */

static void logic_capture_complete_handler(void) { capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE); }

picomso_status_t picomso_handle_request_capture(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                                picomso_response_t *resp) {
    picomso_request_capture_request_t req;
    capture_mode_t active_mode;
    uint32_t max_samples;
    bool capture_started;
    bool has_trigger_payload;
    capture_config_t capture_config = {.total_samples = 0u,
                                       .rate = 0u,
                                       .pre_trigger_samples = 0u,
                                       .channels = 16u};

    set_default_capture_triggers(&capture_config);

    active_mode = capture_controller_get_mode(&s_capture_ctrl);
    if (active_mode != CAPTURE_MODE_LOGIC && active_mode != CAPTURE_MODE_OSCILLOSCOPE) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE, "capture mode not active", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    if (hdr->length < PICOMSO_REQUEST_CAPTURE_LEGACY_SIZE) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "REQUEST_CAPTURE payload too short", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    if (hdr->length != PICOMSO_REQUEST_CAPTURE_LEGACY_SIZE &&
        hdr->length != (uint16_t)sizeof(picomso_request_capture_request_t)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "invalid REQUEST_CAPTURE payload length", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    /* Zero any trigger bytes omitted by the legacy 12-byte payload before we
     * optionally copy or validate the extended trigger array. */
    memset(&req, 0, sizeof(req));
    memcpy(&req, payload, hdr->length);
    max_samples = (active_mode == CAPTURE_MODE_LOGIC) ? LOGIC_CAPTURE_MAX_SAMPLES : SCOPE_CAPTURE_MAX_SAMPLES;
    if (req.total_samples == 0u || req.total_samples > max_samples || req.pre_trigger_samples > req.total_samples) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "invalid capture sizing", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    has_trigger_payload = (hdr->length == (uint16_t)sizeof(picomso_request_capture_request_t));
    if (has_trigger_payload) {
        if (active_mode == CAPTURE_MODE_LOGIC) {
            for (uint32_t i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
                if (!request_trigger_is_valid(&req.trigger[i])) {
                    picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "invalid trigger configuration", resp);
                    return PICOMSO_STATUS_ERR_BAD_LEN;
                }
            }
        }

        copy_request_triggers(&capture_config, &req);
    }

    capture_config.rate = req.rate;
    capture_config.total_samples = req.total_samples;
    capture_config.pre_trigger_samples = req.pre_trigger_samples;

    capture_controller_set_state(&s_capture_ctrl, CAPTURE_RUNNING);
    capture_started = (active_mode == CAPTURE_MODE_LOGIC)
                          ? logic_capture_start(&capture_config, logic_capture_complete_handler)
                          : scope_capture_start(&capture_config);
    if (!capture_started) {
        capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN, "capture request failed", resp);
        return PICOMSO_STATUS_ERR_UNKNOWN;
    }

    picomso_write_ack(hdr->seq, resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * READ_DATA_BLOCK handler
 *
 * Returns one fixed-size chunk from the completed stored capture.
 * No acquisition runs in this handler; it only reads from finalized storage.
 *
 * Thread-safety: this handler, like all handlers in this file, assumes a
 * single-threaded polling context (the main loop in usb_control_plane/main.c).
 * No locking is applied.
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_read_data_block(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                                picomso_response_t *resp) {
    (void)payload; /* READ_DATA_BLOCK carries no request payload */
    capture_mode_t active_mode = capture_controller_get_mode(&s_capture_ctrl);
    bool read_ok;

    if (active_mode != CAPTURE_MODE_LOGIC && active_mode != CAPTURE_MODE_OSCILLOSCOPE) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE, "capture mode not active", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    picomso_data_block_response_t blk;
    uint16_t block_id = 0u;
    uint16_t data_len = 0u;

    memset(&blk, 0, sizeof(blk));
    read_ok = (active_mode == CAPTURE_MODE_LOGIC) ? logic_capture_read_block(&block_id, blk.data, &data_len)
                                                  : scope_capture_read_block(&block_id, blk.data, &data_len);

    if (!read_ok) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN, "no finalized capture data", resp);
        return PICOMSO_STATUS_ERR_UNKNOWN;
    }

    blk.block_id = block_id;
    blk.data_len = data_len;

    capture_controller_set_state(
        &s_capture_ctrl, (active_mode == CAPTURE_MODE_LOGIC) ? logic_capture_get_state() : scope_capture_get_state());

    build_response(hdr, PICOMSO_MSG_DATA_BLOCK, &blk, (uint16_t)sizeof(blk), resp);
    return PICOMSO_STATUS_OK;
}
