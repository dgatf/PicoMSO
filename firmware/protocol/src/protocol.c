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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * PicoMSO unified protocol – dispatch entry point.
 *
 * This file implements:
 *   - picomso_dispatch()    : validate header and route to a handler
 *   - picomso_write_ack()   : build a complete ACK response packet
 *   - picomso_write_error() : build a complete ERROR response packet
 *
 * It has no dependency on any transport (USB, UART, SPI, etc.).
 * Callers are responsible for reading bytes from the transport and
 * writing the response bytes back to it.
 *
 * Stream selection and mixed-signal behavior are handled inside the
 * per-command handlers. This dispatch layer remains transport-agnostic
 * and unaware of capture stream semantics.
 */

#include "protocol.h"

#include <string.h>

/* -----------------------------------------------------------------------
 * Forward declarations for per-command handlers.
 * ----------------------------------------------------------------------- */

picomso_status_t picomso_handle_get_info(const picomso_packet_header_t *hdr,
                                         const uint8_t *payload,
                                         picomso_response_t *resp);

picomso_status_t picomso_handle_get_capabilities(const picomso_packet_header_t *hdr,
                                                 const uint8_t *payload,
                                                 picomso_response_t *resp);

picomso_status_t picomso_handle_get_status(const picomso_packet_header_t *hdr,
                                           const uint8_t *payload,
                                           picomso_response_t *resp);

picomso_status_t picomso_handle_set_mode(const picomso_packet_header_t *hdr,
                                         const uint8_t *payload,
                                         picomso_response_t *resp);

picomso_status_t picomso_handle_request_capture(const picomso_packet_header_t *hdr,
                                                const uint8_t *payload,
                                                picomso_response_t *resp);

picomso_status_t picomso_handle_read_data_block(const picomso_packet_header_t *hdr,
                                                const uint8_t *payload,
                                                picomso_response_t *resp);

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

/* Write a complete packet (header + payload) into resp->buf. */
static void write_packet(picomso_msg_type_t msg_type,
                         uint8_t seq,
                         const void *payload,
                         uint16_t payload_len,
                         picomso_response_t *resp)
{
    picomso_packet_header_t hdr;
    size_t total;

    hdr.magic = PICOMSO_PACKET_MAGIC;
    hdr.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    hdr.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    hdr.msg_type = (uint8_t)msg_type;
    hdr.seq = seq;
    hdr.length = payload_len;

    total = sizeof(hdr) + payload_len;
    if (total > sizeof(resp->buf)) {
        /* Should never happen with well-formed handlers; fail safe. */
        resp->used = 0;
        return;
    }

    memcpy(resp->buf, &hdr, sizeof(hdr));
    if (payload != NULL && payload_len > 0u)
        memcpy(resp->buf + sizeof(hdr), payload, payload_len);

    resp->used = total;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void picomso_write_ack(uint8_t seq, picomso_response_t *resp)
{
    picomso_ack_payload_t ack;

    ack.status = (uint8_t)PICOMSO_STATUS_OK;
    write_packet(PICOMSO_MSG_ACK, seq, &ack, (uint16_t)sizeof(ack), resp);
}

void picomso_write_error(uint8_t seq,
                         picomso_status_t status,
                         const char *msg,
                         picomso_response_t *resp)
{
    picomso_error_payload_t err_hdr;
    picomso_packet_header_t hdr;
    uint8_t msg_len;
    uint16_t payload_len;
    size_t total;

    err_hdr.status = (uint8_t)status;

    msg_len = 0u;
    if (msg != NULL) {
        size_t slen = strlen(msg);
        if (slen > 255u)
            slen = 255u;
        msg_len = (uint8_t)slen;
    }
    err_hdr.msg_len = msg_len;

    payload_len = (uint16_t)(sizeof(err_hdr) + msg_len);

    hdr.magic = PICOMSO_PACKET_MAGIC;
    hdr.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    hdr.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    hdr.msg_type = (uint8_t)PICOMSO_MSG_ERROR;
    hdr.seq = seq;
    hdr.length = payload_len;

    total = sizeof(hdr) + payload_len;
    if (total > sizeof(resp->buf)) {
        resp->used = 0;
        return;
    }

    memcpy(resp->buf, &hdr, sizeof(hdr));
    memcpy(resp->buf + sizeof(hdr), &err_hdr, sizeof(err_hdr));
    if (msg_len > 0u)
        memcpy(resp->buf + sizeof(hdr) + sizeof(err_hdr), msg, msg_len);

    resp->used = total;
}

picomso_status_t picomso_dispatch(const uint8_t *in_buf,
                                  size_t in_len,
                                  picomso_response_t *resp)
{
    picomso_packet_header_t hdr;
    size_t expected_total;
    const uint8_t *payload;

    resp->used = 0;

    /* Minimum length check. */
    if (in_len < PICOMSO_PACKET_HEADER_SIZE) {
        /* No reliable seq available; report with seq = 0. */
        picomso_write_error(0u, PICOMSO_STATUS_ERR_BAD_LEN,
                            "packet too short", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    /* Parse header. */
    memcpy(&hdr, in_buf, sizeof(hdr));

    /* Magic check. */
    if (hdr.magic != PICOMSO_PACKET_MAGIC) {
        picomso_write_error(hdr.seq, PICOMSO_STATUS_ERR_BAD_MAGIC,
                            "bad magic", resp);
        return PICOMSO_STATUS_ERR_BAD_MAGIC;
    }

    /* Version check: major version must match. */
    if (hdr.version_major != PICOMSO_PROTOCOL_VERSION_MAJOR) {
        picomso_write_error(hdr.seq, PICOMSO_STATUS_ERR_VERSION,
                            "incompatible version", resp);
        return PICOMSO_STATUS_ERR_VERSION;
    }

    /* Payload length sanity check. */
    if (hdr.length > PICOMSO_MAX_PAYLOAD_LEN) {
        picomso_write_error(hdr.seq, PICOMSO_STATUS_ERR_BAD_LEN,
                            "payload too large", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    expected_total = PICOMSO_PACKET_HEADER_SIZE + hdr.length;
    if (in_len < expected_total) {
        picomso_write_error(hdr.seq, PICOMSO_STATUS_ERR_BAD_LEN,
                            "truncated payload", resp);
        return PICOMSO_STATUS_ERR_BAD_LEN;
    }

    payload = in_buf + PICOMSO_PACKET_HEADER_SIZE;

    /* Dispatch to the appropriate handler. */
    switch ((picomso_msg_type_t)hdr.msg_type) {
    case PICOMSO_MSG_GET_INFO:
        return picomso_handle_get_info(&hdr, payload, resp);

    case PICOMSO_MSG_GET_CAPABILITIES:
        return picomso_handle_get_capabilities(&hdr, payload, resp);

    case PICOMSO_MSG_GET_STATUS:
        return picomso_handle_get_status(&hdr, payload, resp);

    case PICOMSO_MSG_SET_MODE:
        return picomso_handle_set_mode(&hdr, payload, resp);

    case PICOMSO_MSG_REQUEST_CAPTURE:
        return picomso_handle_request_capture(&hdr, payload, resp);

    case PICOMSO_MSG_READ_DATA_BLOCK:
        return picomso_handle_read_data_block(&hdr, payload, resp);

    default:
        picomso_write_error(hdr.seq, PICOMSO_STATUS_ERR_UNKNOWN,
                            "unknown command", resp);
        return PICOMSO_STATUS_ERR_UNKNOWN;
    }
}