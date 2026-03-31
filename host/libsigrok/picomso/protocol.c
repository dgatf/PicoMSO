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

#include "protocol.h"

#include <stdio.h>
#include <string.h>

static void write_error(char *error_text, size_t error_text_len, const char *message)
{
    if (error_text == NULL || error_text_len == 0u) {
        return;
    }

    snprintf(error_text, error_text_len, "%s", (message != NULL) ? message : "unknown error");
}

size_t picomso_sigrok_build_request(uint8_t *buf,
                                    size_t buf_len,
                                    picomso_msg_type_t msg_type,
                                    uint8_t seq,
                                    const void *payload,
                                    size_t payload_len)
{
    picomso_packet_header_t header;
    size_t total_len;

    if (buf == NULL || payload_len > PICOMSO_MAX_PAYLOAD_LEN) {
        return 0u;
    }

    total_len = sizeof(header) + payload_len;
    if (buf_len < total_len) {
        return 0u;
    }

    header.magic = PICOMSO_PACKET_MAGIC;
    header.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    header.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    header.msg_type = (uint8_t)msg_type;
    header.seq = seq;
    header.length = (uint16_t)payload_len;

    memcpy(buf, &header, sizeof(header));
    if (payload_len > 0u && payload != NULL) {
        memcpy(buf + sizeof(header), payload, payload_len);
    }

    return total_len;
}

bool picomso_sigrok_parse_response(const uint8_t *buf,
                                   size_t buf_len,
                                   uint8_t expected_seq,
                                   picomso_sigrok_response_t *response,
                                   char *error_text,
                                   size_t error_text_len)
{
    picomso_packet_header_t header;

    if (buf == NULL || response == NULL) {
        write_error(error_text, error_text_len, "missing response buffer");
        return false;
    }

    if (buf_len < sizeof(header)) {
        write_error(error_text, error_text_len, "response too short for PicoMSO header");
        return false;
    }

    memcpy(&header, buf, sizeof(header));
    if (header.magic != PICOMSO_PACKET_MAGIC) {
        write_error(error_text, error_text_len, "unexpected PicoMSO packet magic");
        return false;
    }

    if (header.version_major != PICOMSO_PROTOCOL_VERSION_MAJOR) {
        write_error(error_text, error_text_len, "unsupported PicoMSO protocol major version");
        return false;
    }

    if (header.seq != expected_seq) {
        write_error(error_text, error_text_len, "response sequence mismatch");
        return false;
    }

    if (buf_len < sizeof(header) + header.length) {
        write_error(error_text, error_text_len, "truncated PicoMSO response payload");
        return false;
    }

    response->header = header;
    response->payload = buf + sizeof(header);
    return true;
}

bool picomso_sigrok_decode_device_error(const picomso_sigrok_response_t *response,
                                        picomso_sigrok_device_error_t *device_error,
                                        char *error_text,
                                        size_t error_text_len)
{
    const picomso_error_payload_t *error_payload;
    size_t message_len;

    if (response == NULL || device_error == NULL) {
        write_error(error_text, error_text_len, "missing ERROR response storage");
        return false;
    }

    if (response->header.msg_type != PICOMSO_MSG_ERROR) {
        write_error(error_text, error_text_len, "response is not an ERROR packet");
        return false;
    }

    if (response->header.length < sizeof(*error_payload)) {
        write_error(error_text, error_text_len, "short PicoMSO ERROR payload");
        return false;
    }

    error_payload = (const picomso_error_payload_t *)response->payload;
    message_len = error_payload->msg_len;
    if (sizeof(*error_payload) + message_len > response->header.length) {
        write_error(error_text, error_text_len, "truncated PicoMSO ERROR message");
        return false;
    }

    device_error->status = (picomso_status_t)error_payload->status;
    if (message_len >= sizeof(device_error->message)) {
        message_len = sizeof(device_error->message) - 1u;
    }
    memcpy(device_error->message, response->payload + sizeof(*error_payload), message_len);
    device_error->message[message_len] = '\0';
    return true;
}

static bool decode_fixed_payload(const picomso_sigrok_response_t *response,
                                 uint8_t expected_msg_type,
                                 void *dst,
                                 size_t expected_size,
                                 const char *context,
                                 char *error_text,
                                 size_t error_text_len)
{
    if (response == NULL || dst == NULL) {
        write_error(error_text, error_text_len, "missing decode buffer");
        return false;
    }

    if (response->header.msg_type != expected_msg_type) {
        write_error(error_text, error_text_len, context);
        return false;
    }

    if (response->header.length < expected_size) {
        write_error(error_text, error_text_len, "short PicoMSO ACK payload");
        return false;
    }

    memcpy(dst, response->payload, expected_size);
    return true;
}

bool picomso_sigrok_decode_info(const picomso_sigrok_response_t *response,
                                picomso_info_response_t *info,
                                char *error_text,
                                size_t error_text_len)
{
    return decode_fixed_payload(response,
                                PICOMSO_MSG_ACK,
                                info,
                                sizeof(*info),
                                "GET_INFO did not return an ACK payload",
                                error_text,
                                error_text_len);
}

bool picomso_sigrok_decode_capabilities(const picomso_sigrok_response_t *response,
                                        picomso_capabilities_response_t *capabilities,
                                        char *error_text,
                                        size_t error_text_len)
{
    return decode_fixed_payload(response,
                                PICOMSO_MSG_ACK,
                                capabilities,
                                sizeof(*capabilities),
                                "GET_CAPABILITIES did not return an ACK payload",
                                error_text,
                                error_text_len);
}

bool picomso_sigrok_decode_status(const picomso_sigrok_response_t *response,
                                  picomso_status_response_t *status,
                                  char *error_text,
                                  size_t error_text_len)
{
    return decode_fixed_payload(response,
                                PICOMSO_MSG_ACK,
                                status,
                                sizeof(*status),
                                "GET_STATUS did not return an ACK payload",
                                error_text,
                                error_text_len);
}

bool picomso_sigrok_decode_data_block(const picomso_sigrok_response_t *response,
                                      uint16_t *block_id,
                                      const uint8_t **data,
                                      uint16_t *data_len,
                                      char *error_text,
                                      size_t error_text_len)
{
    const picomso_data_block_response_t *block;

    if (response == NULL || block_id == NULL || data == NULL || data_len == NULL) {
        write_error(error_text, error_text_len, "missing DATA_BLOCK decode storage");
        return false;
    }

    if (response->header.msg_type != PICOMSO_MSG_DATA_BLOCK) {
        write_error(error_text, error_text_len, "READ_DATA_BLOCK did not return a DATA_BLOCK packet");
        return false;
    }

    if (response->header.length < 4u) {
        write_error(error_text, error_text_len, "short DATA_BLOCK header");
        return false;
    }

    block = (const picomso_data_block_response_t *)response->payload;
    if ((size_t)4u + block->data_len > response->header.length) {
        write_error(error_text, error_text_len, "truncated DATA_BLOCK payload");
        return false;
    }

    *block_id = block->block_id;
    *data_len = block->data_len;
    *data = block->data;
    return true;
}

bool picomso_sigrok_device_error_is_end_of_capture(const picomso_sigrok_device_error_t *device_error)
{
    if (device_error == NULL) {
        return false;
    }

    return device_error->status == PICOMSO_STATUS_ERR_UNKNOWN &&
           strcmp(device_error->message, "no finalized capture data") == 0;
}
