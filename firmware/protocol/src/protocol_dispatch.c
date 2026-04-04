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
 */

#include <string.h>

#include "capture_controller.h"
#include "debug.h"
#include "logic_capture.h"
#include "mixed_capture.h"
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
 *
 * In a future phase this may be read from hardware-detection logic.
 * ----------------------------------------------------------------------- */

#define PICOMSO_STATIC_CAPABILITIES (PICOMSO_CAP_LOGIC | PICOMSO_CAP_SCOPE)

/* -----------------------------------------------------------------------
 * Module-level capture controller instance.
 *
 * Starts in PICOMSO_STREAM_NONE / CAPTURE_IDLE.
 * ----------------------------------------------------------------------- */

static capture_controller_t s_capture_ctrl = {.streams_enabled = PICOMSO_STREAM_NONE, .state = CAPTURE_IDLE};

/* -----------------------------------------------------------------------
 * Minimal mixed-mode runtime tracking.
 *
 * This is a temporary protocol-level orchestration layer so that mixed
 * logic+scope capture can be exercised before a dedicated TX scheduler
 * exists.
 * ----------------------------------------------------------------------- */

static volatile bool s_logic_capture_done = false;
static volatile bool s_scope_capture_done = false;

typedef enum mixed_read_phase_t {
    MIXED_READ_PHASE_LOGIC = 0,
    MIXED_READ_PHASE_SCOPE,
    MIXED_READ_PHASE_DONE
} mixed_read_phase_t;

static mixed_read_phase_t s_mixed_read_phase = MIXED_READ_PHASE_LOGIC;

static const char *stream_name(uint8_t streams) {
    switch (streams) {
        case PICOMSO_STREAM_NONE:
            return "none";
        case PICOMSO_STREAM_LOGIC:
            return "logic";
        case PICOMSO_STREAM_SCOPE:
            return "scope";
        case (PICOMSO_STREAM_LOGIC | PICOMSO_STREAM_SCOPE):
            return "logic|scope";
        default:
            return "invalid";
    }
}

static const char *capture_state_name(capture_state_t state) {
    switch (state) {
        case CAPTURE_IDLE:
            return "IDLE";
        case CAPTURE_RUNNING:
            return "RUNNING";
        default:
            return "UNKNOWN";
    }
}

static uint32_t request_trigger_count(const picomso_request_capture_request_t *request) {
    uint32_t count = 0u;
    uint32_t i;

    for (i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
        if (request->trigger[i].is_enabled != 0u) ++count;
    }

    return count;
}

static uint8_t data_block_flags_for_streams(uint8_t streams, bool terminal) {
    uint8_t flags = 0u;

    if ((streams & PICOMSO_STREAM_LOGIC) != 0u && s_logic_capture_done)
        flags |= PICOMSO_DATA_BLOCK_FLAG_LOGIC_FINALIZED;

    if ((streams & PICOMSO_STREAM_SCOPE) != 0u && s_scope_capture_done)
        flags |= PICOMSO_DATA_BLOCK_FLAG_SCOPE_FINALIZED;

    if (terminal) flags |= PICOMSO_DATA_BLOCK_FLAG_TERMINAL;

    return flags;
}

static uint8_t terminal_stream_id_for_streams(uint8_t streams) {
    if ((streams & PICOMSO_STREAM_SCOPE) != 0u) return PICOMSO_STREAM_ID_SCOPE;

    return PICOMSO_STREAM_ID_LOGIC;
}

static void build_response(const picomso_packet_header_t *req_hdr, picomso_msg_type_t resp_type, const void *payload,
                           uint16_t payload_len, picomso_response_t *resp) {
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
    if (payload != NULL && payload_len > 0u) memcpy(resp->buf + sizeof(out_hdr), payload, payload_len);

    resp->used = total;
}

static void build_terminal_data_block(const picomso_packet_header_t *hdr, uint8_t active_streams,
                                      picomso_response_t *resp) {
    picomso_data_block_response_t blk;

    memset(&blk, 0, sizeof(blk));
    blk.stream_id = terminal_stream_id_for_streams(active_streams);
    blk.flags = data_block_flags_for_streams(active_streams, true);
    blk.block_id = 0u;
    blk.data_len = 0u;

    build_response(hdr, PICOMSO_MSG_DATA_BLOCK, &blk,
                   (uint16_t)(sizeof(blk.stream_id) + sizeof(blk.flags) + sizeof(blk.block_id) + sizeof(blk.data_len)),
                   resp);
}

/* -----------------------------------------------------------------------
 * Internal helper: build a full response packet.
 *
 * Copies hdr.seq into the response, sets msg_type to resp_type, and
 * appends payload_len bytes from payload.
 * ----------------------------------------------------------------------- */

static bool stream_mask_is_valid(uint8_t streams) {
    const uint8_t valid_mask = PICOMSO_STREAM_LOGIC | PICOMSO_STREAM_SCOPE;
    return (streams & (uint8_t)~valid_mask) == 0u;
}

static bool stream_mask_is_mixed(uint8_t streams) { return streams == (PICOMSO_STREAM_LOGIC | PICOMSO_STREAM_SCOPE); }

static bool request_trigger_is_valid(const picomso_trigger_config_t *trigger) {
    if (trigger->is_enabled > 1u || trigger->pin >= PICOMSO_LOGIC_TRIGGER_PIN_COUNT) return false;

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

static trigger_match_t protocol_trigger_match_to_internal(picomso_trigger_match_t match) {
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

static void copy_request_triggers(capture_config_t *capture_config, const picomso_request_capture_request_t *request) {
    uint32_t i;

    for (i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
        capture_config->trigger[i].is_enabled = (request->trigger[i].is_enabled != 0u);
        capture_config->trigger[i].pin = request->trigger[i].pin;
        capture_config->trigger[i].match =
            protocol_trigger_match_to_internal((picomso_trigger_match_t)request->trigger[i].match);
    }
}

static void mixed_capture_reset_tracking(void) {
    s_logic_capture_done = false;
    s_scope_capture_done = false;
    s_mixed_read_phase = MIXED_READ_PHASE_LOGIC;
}

static void update_controller_state_from_backends(uint8_t streams) {
    capture_state_t logic_state = CAPTURE_IDLE;
    capture_state_t scope_state = CAPTURE_IDLE;

    if ((streams & PICOMSO_STREAM_LOGIC) != 0u) logic_state = logic_capture_get_state();

    if ((streams & PICOMSO_STREAM_SCOPE) != 0u) scope_state = scope_capture_get_state();

    if (stream_mask_is_mixed(streams)) {
        if (logic_state == CAPTURE_RUNNING || scope_state == CAPTURE_RUNNING)
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_RUNNING);
        else
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
    } else if (streams == PICOMSO_STREAM_LOGIC) {
        capture_controller_set_state(&s_capture_ctrl, logic_state);
    } else if (streams == PICOMSO_STREAM_SCOPE) {
        capture_controller_set_state(&s_capture_ctrl, scope_state);
    } else {
        capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
    }
}

/* -----------------------------------------------------------------------
 * GET_INFO handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_info(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                         picomso_response_t *resp) {
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

picomso_status_t picomso_handle_get_capabilities(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                                 picomso_response_t *resp) {
    picomso_capabilities_response_t caps;

    (void)payload;

    caps.capabilities = PICOMSO_STATIC_CAPABILITIES;

    build_response(hdr, PICOMSO_MSG_ACK, &caps, (uint16_t)sizeof(caps), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * GET_STATUS handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_status(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                           picomso_response_t *resp) {
    picomso_status_response_t status;
    uint8_t streams;
    capture_state_t controller_before;

    (void)payload;

    streams = capture_controller_get_streams(&s_capture_ctrl);
    controller_before = capture_controller_get_state(&s_capture_ctrl);

    update_controller_state_from_backends(streams);

    status.streams = streams;
    status.capture_state = (uint8_t)capture_controller_get_state(&s_capture_ctrl);

    debug("\n[protocol] GET_STATUS streams=%s ctrl_before=%s result=%s logic_done=%u scope_done=%u",
          stream_name(streams), capture_state_name(controller_before),
          capture_state_name((capture_state_t)status.capture_state), s_logic_capture_done ? 1u : 0u,
          s_scope_capture_done ? 1u : 0u);

    build_response(hdr, PICOMSO_MSG_ACK, &status, (uint16_t)sizeof(status), resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * SET_MODE handler
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_set_mode(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                         picomso_response_t *resp) {
    picomso_set_mode_request_t req;
    uint8_t prev_streams = capture_controller_get_streams(&s_capture_ctrl);
    capture_state_t prev_state = capture_controller_get_state(&s_capture_ctrl);

    if (hdr->length < (uint16_t)sizeof(req)) {
        debug("\n[protocol] SET_MODE rejected reason=payload_too_short payload_len=%u", hdr->length);
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "SET_MODE payload too short", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    memcpy(&req, payload, sizeof(req));

    debug("\n[protocol] SET_MODE request streams=%s prev_streams=%s prev_state=%s", stream_name(req.streams),
          stream_name(prev_streams), capture_state_name(prev_state));

    if (!stream_mask_is_valid(req.streams)) {
        debug("\n[protocol] SET_MODE rejected reason=invalid_stream_mask streams=0x%02x", req.streams);
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE, "invalid stream mask", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    mixed_capture_reset();
    mixed_capture_reset_tracking();

    capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
    capture_controller_set_streams(&s_capture_ctrl, req.streams);

    debug("\n[protocol] SET_MODE applied streams=%s state=%s",
          stream_name(capture_controller_get_streams(&s_capture_ctrl)),
          capture_state_name(capture_controller_get_state(&s_capture_ctrl)));

    picomso_write_ack(hdr->seq, resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * REQUEST_CAPTURE handler
 *
 * Starts a capture for the currently selected stream.
 * ----------------------------------------------------------------------- */

static void logic_capture_complete_handler(void) {
    s_logic_capture_done = true;

    if (capture_controller_get_streams(&s_capture_ctrl) == PICOMSO_STREAM_LOGIC) {
        capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
    } else if (stream_mask_is_mixed(capture_controller_get_streams(&s_capture_ctrl))) {
        if (s_logic_capture_done && s_scope_capture_done)
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
        else
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_RUNNING);
    }

    debug("\n[protocol] capture_complete backend=logic streams=%s state=%s logic_done=%u scope_done=%u",
          stream_name(capture_controller_get_streams(&s_capture_ctrl)),
          capture_state_name(capture_controller_get_state(&s_capture_ctrl)), s_logic_capture_done ? 1u : 0u,
          s_scope_capture_done ? 1u : 0u);
}

static void scope_capture_complete_handler(void) {
    s_scope_capture_done = true;

    if (capture_controller_get_streams(&s_capture_ctrl) == PICOMSO_STREAM_SCOPE) {
        capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
    } else if (stream_mask_is_mixed(capture_controller_get_streams(&s_capture_ctrl))) {
        if (s_logic_capture_done && s_scope_capture_done)
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
        else
            capture_controller_set_state(&s_capture_ctrl, CAPTURE_RUNNING);
    }

    debug("\n[protocol] capture_complete backend=scope streams=%s state=%s logic_done=%u scope_done=%u",
          stream_name(capture_controller_get_streams(&s_capture_ctrl)),
          capture_state_name(capture_controller_get_state(&s_capture_ctrl)), s_logic_capture_done ? 1u : 0u,
          s_scope_capture_done ? 1u : 0u);
}

picomso_status_t picomso_handle_request_capture(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                                picomso_response_t *resp) {
    picomso_request_capture_request_t req;
    uint8_t active_streams;
    uint32_t logic_max_samples = LOGIC_CAPTURE_MAX_SAMPLES;
    uint32_t scope_max_samples = SCOPE_CAPTURE_MAX_SAMPLES;
    bool capture_started = false;
    capture_config_t capture_config = {.total_samples = 0u, .rate = 0u, .pre_trigger_samples = 0u, .channels = 16u};
    uint32_t i;
    uint8_t analog_ch;

    /* sizeof(req) includes the trailing analog_channels byte (25 bytes total).
     * Also accept the legacy 24-byte form (without analog_channels) from older
     * hosts; in that case default to ADC input 0 only. */
    static const uint16_t legacy_len =
        (uint16_t)(sizeof(picomso_request_capture_request_t) - 1u);

    active_streams = capture_controller_get_streams(&s_capture_ctrl);

    if (active_streams == PICOMSO_STREAM_NONE) {
        debug("\n[protocol] REQUEST_CAPTURE rejected reason=no_active_stream");
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE, "capture stream not active", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    if (hdr->length != (uint16_t)sizeof(req) && hdr->length != legacy_len) {
        debug("\n[protocol] REQUEST_CAPTURE rejected reason=bad_payload_len payload_len=%u expected=%u or %u",
              hdr->length, (unsigned int)sizeof(req), (unsigned int)legacy_len);
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "invalid REQUEST_CAPTURE payload length", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    if (capture_controller_get_state(&s_capture_ctrl) == CAPTURE_RUNNING) {
        debug("\n[protocol] REQUEST_CAPTURE rejected reason=already_running streams=%s", stream_name(active_streams));
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN, "capture already running", resp);
        return PICOMSO_STATUS_ERR_UNKNOWN;
    }

    memset(&req, 0, sizeof(req));
    memcpy(&req, payload, hdr->length);

    /* Resolve analog_channels: legacy packet has 0x00 here, treat as ADC0 only. */
    analog_ch = (req.analog_channels == 0u) ? 0x01u : req.analog_channels;

    debug("\n[protocol] REQUEST_CAPTURE request streams=%s samples=%lu rate=%lu pre=%lu triggers=%lu analog_ch=0x%02x",
          stream_name(active_streams), (unsigned long)req.total_samples, (unsigned long)req.rate,
          (unsigned long)req.pre_trigger_samples, (unsigned long)request_trigger_count(&req), (unsigned)analog_ch);

    if ((active_streams & PICOMSO_STREAM_LOGIC) != 0u) {
        if (req.total_samples == 0u || req.total_samples > logic_max_samples ||
            req.pre_trigger_samples > req.total_samples) {
            debug(
                "\n[protocol] REQUEST_CAPTURE rejected reason=invalid_logic_capture_sizing samples=%lu pre=%lu max=%lu",
                (unsigned long)req.total_samples, (unsigned long)req.pre_trigger_samples,
                (unsigned long)logic_max_samples);
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "invalid logic capture sizing", resp);
            return PICOMSO_STATUS_ERR_BAD_LEN;
        }
    }

    if ((active_streams & PICOMSO_STREAM_SCOPE) != 0u) {
        if ((analog_ch & ~0x07u) != 0u) {
            debug("\n[protocol] REQUEST_CAPTURE rejected reason=invalid_analog_channels analog_ch=0x%02x",
                  (unsigned)analog_ch);
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "invalid analog_channels bitmask", resp);
            return PICOMSO_STATUS_ERR_BAD_LEN;
        }

        if (req.total_samples == 0u || req.total_samples > scope_max_samples ||
            req.pre_trigger_samples > req.total_samples ||
            req.pre_trigger_samples > SCOPE_CAPTURE_PRE_TRIGGER_MAX_SAMPLES) {
            debug(
                "\n[protocol] REQUEST_CAPTURE rejected reason=invalid_scope_capture_sizing samples=%lu pre=%lu max=%lu "
                "pre_max=%u",
                (unsigned long)req.total_samples, (unsigned long)req.pre_trigger_samples,
                (unsigned long)scope_max_samples, SCOPE_CAPTURE_PRE_TRIGGER_MAX_SAMPLES);
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "invalid scope capture sizing", resp);
            return PICOMSO_STATUS_ERR_BAD_LEN;
        }
    }

    for (i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
        if (!request_trigger_is_valid(&req.trigger[i])) {
            debug("\n[protocol] REQUEST_CAPTURE rejected reason=invalid_trigger index=%lu", (unsigned long)i);
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_LEN, "invalid trigger configuration", resp);
            return PICOMSO_STATUS_ERR_BAD_LEN;
        }
    }

    copy_request_triggers(&capture_config, &req);
    capture_config.rate = req.rate;
    capture_config.total_samples = req.total_samples;
    capture_config.pre_trigger_samples = req.pre_trigger_samples;

    /*
     * For logic captures, channels is the parallel input count (16).
     * For scope captures, channels is the ADC input bitmask derived from
     * analog_channels (bit 0 = ADC0, bit 1 = ADC1, bit 2 = ADC2).  The
     * firmware round-robins only the selected inputs in ascending index order.
     * The interleaved sample stream is demultiplexed by the host driver.
     */
    if ((active_streams & PICOMSO_STREAM_SCOPE) != 0u) {
        capture_config.channels = (uint)analog_ch;
    } else {
        capture_config.channels = 16u;
    }

    mixed_capture_reset_tracking();
    capture_controller_set_state(&s_capture_ctrl, CAPTURE_RUNNING);

    if (active_streams == PICOMSO_STREAM_LOGIC) {
        capture_started = logic_capture_start(&capture_config, logic_capture_complete_handler);
    } else if (active_streams == PICOMSO_STREAM_SCOPE) {
        capture_started = scope_capture_start(&capture_config, scope_capture_complete_handler);
    } else if (stream_mask_is_mixed(active_streams)) {
        capture_started = mixed_capture_start(&capture_config, &capture_config, logic_capture_complete_handler,
                                              scope_capture_complete_handler);
    }

    if (!capture_started) {
        mixed_capture_reset_tracking();
        capture_controller_set_state(&s_capture_ctrl, CAPTURE_IDLE);
        debug("\n[protocol] REQUEST_CAPTURE start_failed backend=%s state=%s", stream_name(active_streams),
              capture_state_name(capture_controller_get_state(&s_capture_ctrl)));
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN, "capture request failed", resp);
        return PICOMSO_STATUS_ERR_UNKNOWN;
    }

    debug("\n[protocol] REQUEST_CAPTURE started backend=%s state=%s", stream_name(active_streams),
          capture_state_name(capture_controller_get_state(&s_capture_ctrl)));

    picomso_write_ack(hdr->seq, resp);
    return PICOMSO_STATUS_OK;
}

/* -----------------------------------------------------------------------
 * READ_DATA_BLOCK handler
 *
 * Returns one fixed-size chunk from the completed stored capture.
 * No acquisition runs in this handler; it only reads from finalized storage.
 *
 * In mixed mode, this minimal implementation alternates between logic and
 * scope streams and relies on per-block stream_id for host-side parsing.
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_read_data_block(const picomso_packet_header_t *hdr, const uint8_t *payload,
                                                picomso_response_t *resp) {
    uint8_t active_streams;
    bool read_ok = false;
    picomso_data_block_response_t blk;
    uint16_t block_id;
    uint16_t data_len;
    uint16_t payload_len;

    (void)payload;

    active_streams = capture_controller_get_streams(&s_capture_ctrl);

    debug("\n[protocol] READ_DATA_BLOCK request streams=%s state=%s logic_done=%u scope_done=%u",
          stream_name(active_streams), capture_state_name(capture_controller_get_state(&s_capture_ctrl)),
          s_logic_capture_done ? 1u : 0u, s_scope_capture_done ? 1u : 0u);

    if (active_streams == PICOMSO_STREAM_NONE) {
        debug("\n[protocol] READ_DATA_BLOCK rejected reason=no_active_stream");
        picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_BAD_MODE, "capture stream not active", resp);
        return PICOMSO_STATUS_ERR_BAD_MODE;
    }

    memset(&blk, 0, sizeof(blk));
    block_id = 0u;
    data_len = 0u;

    if (active_streams == PICOMSO_STREAM_LOGIC) {
        if (!s_logic_capture_done) {
            debug("\n[protocol] READ_DATA_BLOCK rejected reason=logic_not_finalized");
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN, "logic capture still running", resp);
            return PICOMSO_STATUS_ERR_UNKNOWN;
        }

        blk.stream_id = PICOMSO_STREAM_ID_LOGIC;
        read_ok = logic_capture_read_block(&block_id, blk.data, &data_len);

        if (!read_ok) {
            debug("\n[protocol] READ_DATA_BLOCK terminal stream=logic");
            build_terminal_data_block(hdr, active_streams, resp);
            return PICOMSO_STATUS_OK;
        }
    } else if (active_streams == PICOMSO_STREAM_SCOPE) {
        if (!s_scope_capture_done) {
            debug("\n[protocol] READ_DATA_BLOCK rejected reason=scope_not_finalized");
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN, "scope capture still running", resp);
            return PICOMSO_STATUS_ERR_UNKNOWN;
        }

        blk.stream_id = PICOMSO_STREAM_ID_SCOPE;
        read_ok = scope_capture_read_block(&block_id, blk.data, &data_len);

        if (!read_ok) {
            debug("\n[protocol] READ_DATA_BLOCK terminal stream=scope");
            build_terminal_data_block(hdr, active_streams, resp);
            return PICOMSO_STATUS_OK;
        }
    } else {
        /*
         * Mixed-mode contract:
         * - Readout starts only after both streams are finalized.
         * - Drain logic fully first.
         * - Drain scope fully second.
         * - Finish with a terminal zero-length DATA_BLOCK.
         */
        if (!s_logic_capture_done || !s_scope_capture_done) {
            debug("\n[protocol] READ_DATA_BLOCK rejected reason=mixed_not_finalized logic_done=%u scope_done=%u",
                  s_logic_capture_done ? 1u : 0u, s_scope_capture_done ? 1u : 0u);
            picomso_write_error(hdr->seq, PICOMSO_STATUS_ERR_UNKNOWN, "mixed capture not finalized", resp);
            return PICOMSO_STATUS_ERR_UNKNOWN;
        }

        if (s_mixed_read_phase == MIXED_READ_PHASE_LOGIC) {
            blk.stream_id = PICOMSO_STREAM_ID_LOGIC;
            read_ok = logic_capture_read_block(&block_id, blk.data, &data_len);

            if (!read_ok) {
                s_mixed_read_phase = MIXED_READ_PHASE_SCOPE;
                blk.stream_id = PICOMSO_STREAM_ID_SCOPE;
                read_ok = scope_capture_read_block(&block_id, blk.data, &data_len);
            }
        } else if (s_mixed_read_phase == MIXED_READ_PHASE_SCOPE) {
            blk.stream_id = PICOMSO_STREAM_ID_SCOPE;
            read_ok = scope_capture_read_block(&block_id, blk.data, &data_len);
        } else {
            read_ok = false;
        }

        if (!read_ok) {
            if (s_mixed_read_phase == MIXED_READ_PHASE_SCOPE) s_mixed_read_phase = MIXED_READ_PHASE_DONE;

            debug("\n[protocol] READ_DATA_BLOCK terminal stream=mixed");
            build_terminal_data_block(hdr, active_streams, resp);
            return PICOMSO_STATUS_OK;
        }

        if (blk.stream_id == PICOMSO_STREAM_ID_SCOPE) s_mixed_read_phase = MIXED_READ_PHASE_SCOPE;
    }

    update_controller_state_from_backends(active_streams);

    blk.flags = data_block_flags_for_streams(active_streams, false);
    blk.block_id = block_id;
    blk.data_len = data_len;

    payload_len = (uint16_t)(sizeof(blk.stream_id) + sizeof(blk.flags) + sizeof(blk.block_id) + sizeof(blk.data_len) +
                             blk.data_len);

    debug("\n[protocol] READ_DATA_BLOCK result stream_id=%u block=%u data_len=%u flags=0x%02x ctrl_state=%s",
          blk.stream_id, block_id, data_len, blk.flags,
          capture_state_name(capture_controller_get_state(&s_capture_ctrl)));

    build_response(hdr, PICOMSO_MSG_DATA_BLOCK, &blk, payload_len, resp);
    return PICOMSO_STATUS_OK;
}