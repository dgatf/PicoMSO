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
 * Each function in this file handles one message type. Handlers build
 * a response packet into the supplied picomso_response_t buffer and
 * return the appropriate picomso_status_t.
 *
 * GET_STATUS, SET_MODE, and REQUEST_CAPTURE delegate to a module-level
 * capture_controller_t instance so that the protocol layer reflects
 * real capture stream/state.
 *
 * GET_INFO and GET_CAPABILITIES continue to return static values.
 *
 * For now, the control plane already uses a stream bitmask, but the
 * mixed combination LOGIC|SCOPE is still reported as unsupported.
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
 * Starts in PICOMSO_STREAM_NONE / CAPTURE_IDLE.
 * ----------------------------------------------------------------------- */

static capture_controller_t s_capture_ctrl = {
    .streams_enabled = PICOMSO_STREAM_NONE,
    .state = CAPTURE_IDLE
};

/* -----------------------------------------------------------------------
 * Internal helper: build a full response packet.
 *
 * Copies hdr.seq into the response, sets msg_type to resp_type, and
 * appends payload_len bytes from payload.
 * ----------------------------------------------------------------------- */

static void build_response(const picomso_packet_header_t *req_hdr,
                           picomso_msg_type_t resp_type,
                           const void *payload,
                           uint16_t payload_len,
                           picomso_response_t *resp)
{
    picomso_packet_header_t out_hdr;
    size_t total;

    out_hdr.magic = PICOMSO_PACKET_MAGIC;
    out_hdr.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    out_hdr.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    out_hdr.msg_type = (uint8_t)resp_type;
    out_hdr.seq = req_hdr->seq;
    out_hdr.length = payload_len;

    total = sizeof(out_hdr) + payload_len;
    if (total > sizeof(resp->buf)) {
        resp->used = 0;
        return;
    }

    memcpy(resp->buf, &out_hdr, sizeof(out_hdr));
    if (payload != NULL && payload_len > 0u)
        memcpy(resp->buf + sizeof(out_hdr), payload, payload_len);

    resp->used = total;
}

static bool stream_mask_is_valid(uint8_t streams)
{
    const uint8_t valid_mask = PICOMSO_STREAM_LOGIC | PICOMSO_STREAM_SCOPE;
    return (streams & (uint8_t)~valid_mask) == 0u;
}

static bool stream_mask_is_mixed(uint8_t streams)
{
    return streams == (PICOMSO_STREAM_LOGIC | PICOMSO_STREAM_SCOPE);
}

static bool request_trigger_is_valid(const picomso_trigger_config_t *trigger)
{
    if (trigger->is_enabled > 1u || trigger->pin >= PICOMSO_LOGIC_TRIGGER_PIN_COUNT)
        return false;

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

static trigger_match_t protocol_trigger_match_to_internal(picomso_trigger_match_t match)
{
    switch (match) {
    case PICOMSO_TRIGGER_MATCH_LEVEL_LOW:
        return TRIGGER_TYPE_LEVEL_LOW;
    case PICOMSO_TRIGGER_MATCH_LEVEL_HIGH:
        return TRIGGER_TYPE_LEVEL_HIGH;
    case PICOMSO_TRIGGER_MATCH_EDGE_LOW:
        return TRIGGER_TYPE_EDGE_LOW;
    case PICOMSO_TRIGGER_MATCH_EDGE_HIGH:
        return TRIGGER_TYPE_EDGE_HIGH;
    default:
        return TRIGGER_TYPE_LEVEL_LOW;
    }
}

static void copy_request_triggers(capture_config_t *capture_config,
                                  const picomso_request_capture_request_t *request)
{
    uint32_t i;

    for (i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
        capture_config->trigger[i].is_enabled = (request->trigger[i].is_enabled != 0u);
        capture_config->trigger[i].pin = request->trigger[i].pin;
        capture_config->trigger[i].match =
            protocol_trigger_match_to_internal(
                (picomso_trigger_match_t)request->trigger[i].match);
    }
}

/* -----------------------------------------------------------------------
 * GET_INFO handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_info(const picomso_packet_header_t *hdr,
                                         const uint8_t *payload,
                                         picomso_response_t *resp)
{
    picomso_info_response_t info;

    (void)payload;

    info.protocol_version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    info.protocol_version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;

    memset(info.fw_id, 0, sizeof(info.fw_id));
    strncpy(info.fw_id, PICOMSO_FW_ID, sizeof(info.fw_id) - 1u);

    build_response(hdr, PICOMSO_MSG_ACK, &info, (uint16_t)sizeof(info), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * GET_CAPABILITIES handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_capabilities(const picomso_packet_header_t *hdr,
                                                 const uint8_t *payload,
                                                 picomso_response_t *resp)
{
    picomso_capabilities_response_t caps;

    (void)payload;

    caps.capabilities = PICOMSO_STATIC_CAPABILITIES;

    build_response(hdr, PICOMSO_MSG_ACK, &caps, (uint16_t)sizeof(caps), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * GET_STATUS handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_status(const picomso_packet_header_t *hdr,
                                           const uint8_t *payload,
                                           picomso_response_t *resp)
{
    picomso_status_response_t status;
    uint8_t streams;

    (void)payload;

    streams = capture_controller_get_streams(&s_capture_ctrl);

    if (streams == PICOMSO_STREAM_LOGIC) {
        capture_controller_set_state(&s_capture_ctrl, logic_capture_get_state());
    } else if (streams == PICOMSO_STREAM_SCOPE) {
        capture_controller_set_state(&s_capture_ctrl, scope_capture_get_state());
    } else {
        capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
    }

    status.streams = streams;
    status.capture_state = (uint8_t)capture_controller_get_state(&s_capture_ctrl);

    build_response(hdr, PICOMSO_MSG_ACK, &status, (uint16_t)sizeof(status), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * SET_MODE handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_set_mode(const picomso_packet_header_t *hdr,
                                         const uint8_t *payload,
                                         picomso_response_t *resp)
{
    picomso_set_mode_request_t req;

    if (hdr->length < (uint16_t)sizeof(req)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN,
                            "SET_MODE payload too short", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    memcpy(&req, payload, sizeof(req));

    if (!stream_mask_is_valid(req.streams)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE,
                            "invalid stream mask", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    if (stream_mask_is_mixed(req.streams)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE,
                            "mixed stream combination not supported yet", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    logic_capture_reset();
    scope_capture_reset();
    capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
    capture_controller_set_streams(&s_capture_ctrl, req.streams);

    picomso_write_ack(hdr->seq, resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * REQUEST_CAPTURE handler
 *
 * Starts a capture for the currently selected stream.
 *
 * The control plane already accepts a stream bitmask, but for now only
 * single-stream capture is supported. Mixed LOGIC|SCOPE is explicitly
 * rejected until the TX orchestration layer exists.
 * ----------------------------------------------------------------------- */

static void logic_capture_complete_handler(void)
{
    capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
}

static void scope_capture_complete_handler(void)
{
    capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
}

picomso_status_t picomso_handle_request_capture(const picomso_packet_header_t *hdr,
                                                const uint8_t *payload,
                                                picomso_response_t *resp)
{
    picomso_request_capture_request_t req;
    uint8_t active_streams;
    uint32_t max_samples;
    bool capture_started;
    capture_config_t capture_config = {
        .total_samples = 0u,
        .rate = 0u,
        .pre_trigger_samples = 0u,
        .channels = 16u
    };
    uint32_t i;

    active_streams = capture_controller_get_streams(&s_capture_ctrl);

    if (active_streams == PICOMSO_STREAM_NONE) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE,
                            "capture stream not active", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    if (stream_mask_is_mixed(active_streams)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE,
                            "mixed stream combination not supported yet", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    if (hdr->length != (uint16_t)sizeof(req)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN,
                            "invalid REQUEST_CAPTURE payload length", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    if (capture_controller_get_state(&s_capture_ctrl) == CAPTURE_RUNNING) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN,
                            "capture already running", resp);
        return PICOMSO_STATUS_ERR_UNKNOWN;
    }

    memcpy(&req, payload, sizeof(req));

    max_samples = (active_streams == PICOMSO_STREAM_LOGIC)
                    ? LOGIC_CAPTURE_MAX_SAMPLES
                    : SCOPE_CAPTURE_MAX_SAMPLES;

    if (req.total_samples == 0u ||
        req.total_samples > max_samples ||
        req.pre_trigger_samples > req.total_samples) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN,
                            "invalid capture sizing", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    for (i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
        if (!request_trigger_is_valid(&req.trigger[i])) {
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN,
                                "invalid trigger configuration", resp);
            return PICOMSO_STATUS_ERR_BAD_LEN;
        }
    }

    copy_request_triggers(&capture_config, &req);
    capture_config.rate = req.rate;
    capture_config.total_samples = req.total_samples;
    capture_config.pre_trigger_samples = req.pre_trigger_samples;

    capture_controller_set_state(&s_capture_ctrl, CAPTURE_RUNNING);

    capture_started = (active_streams == PICOMSO_STREAM_LOGIC)
        ? logic_capture_start(&capture_config, logic_capture_complete_handler)
        : scope_capture_start(&capture_config, scope_capture_complete_handler);

    if (!capture_started) {
        capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN,
                            "capture request failed", resp);
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
 * For now, only single-stream operation is supported, but each returned
 * block already carries an explicit stream_id for forward-compatible mixed
 * mode parsing on the host.
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_read_data_block(const picomso_packet_header_t *hdr,
                                                const uint8_t *payload,
                                                picomso_response_t *resp)
{
    uint8_t active_streams;
    bool read_ok;
    picomso_data_block_response_t blk;
    uint16_t block_id;
    uint16_t data_len;
    uint16_t payload_len;

    (void)payload;

    active_streams = capture_controller_get_streams(&s_capture_ctrl);

    if (active_streams == PICOMSO_STREAM_NONE) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE,
                            "capture stream not active", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    if (stream_mask_is_mixed(active_streams)) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE,
                            "mixed stream combination not supported yet", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    memset(&blk, 0, sizeof(blk));
    block_id = 0u;
    data_len = 0u;

    if (active_streams == PICOMSO_STREAM_LOGIC) {
        blk.stream_id = PICOMSO_STREAM_ID_LOGIC;
        read_ok = logic_capture_read_block(&block_id, blk.data, &data_len);
        capture_controller_set_state(&s_capture_ctrl, logic_capture_get_state());
    } else {
        blk.stream_id = PICOMSO_STREAM_ID_SCOPE;
        read_ok = scope_capture_read_block(&block_id, blk.data, &data_len);
        capture_controller_set_state(&s_capture_ctrl, scope_capture_get_state());
    }

    if (!read_ok) {
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN,
                            "no finalized capture data", resp);
        return PICOMSO_STATUS_ERR_UNKNOWN;
    }

    blk.flags = 0u;
    blk.block_id = block_id;
    blk.data_len = data_len;

    payload_len = (uint16_t)(
        sizeof(blk.stream_id) +
        sizeof(blk.flags) +
        sizeof(blk.block_id) +
        sizeof(blk.data_len) +
        blk.data_len);

    build_response(hdr, PICOMSO_MSG_DATA_BLOCK, &blk, payload_len, resp);
    return PICOMSO_STATUS_OK;
}