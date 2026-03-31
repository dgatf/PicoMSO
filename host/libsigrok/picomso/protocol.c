/*
 * PicoMSO - libsigrok-style host protocol layer
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "protocol.h"

#include <string.h>

#define PICOMSO_RESPONSE_TYPE_INFO          PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_CAPABILITIES  PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_STATUS        PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_SET_MODE      PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_REQUEST       PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_DATA_BLOCK    PICOMSO_MSG_DATA_BLOCK

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    data[2] = (uint8_t)((value >> 16) & 0xffu);
    data[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void clear_error_state(picomso_protocol_t *proto)
{
    proto->last_device_status = (uint8_t)PICOMSO_STATUS_OK;
    proto->last_error_text[0] = '\0';
}

static void set_error_text(picomso_protocol_t *proto, const char *text, size_t length)
{
    size_t copy_len = length;

    if (copy_len >= sizeof(proto->last_error_text)) {
        copy_len = sizeof(proto->last_error_text) - 1u;
    }

    if (copy_len > 0u && text != NULL) {
        memcpy(proto->last_error_text, text, copy_len);
    }
    proto->last_error_text[copy_len] = '\0';
}

static picomso_result_t send_request(picomso_protocol_t *proto,
                                     uint8_t msg_type,
                                     const uint8_t *payload,
                                     uint16_t payload_len,
                                     uint8_t expected_response_type,
                                     uint8_t *response,
                                     size_t *response_len)
{
    uint8_t request[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t actual_length = 0u;
    uint8_t seq;
    uint16_t wire_length;

    if (proto == NULL || response == NULL || response_len == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }
    if (proto->transport.control_write == NULL || proto->transport.bulk_read == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }
    if ((size_t)PICOMSO_PACKET_HEADER_SIZE + payload_len > sizeof(request)) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }

    clear_error_state(proto);
    seq = proto->next_seq++;

    write_u16_le(request, PICOMSO_PACKET_MAGIC);
    request[2] = PICOMSO_PROTOCOL_VERSION_MAJOR;
    request[3] = PICOMSO_PROTOCOL_VERSION_MINOR;
    request[4] = msg_type;
    request[5] = seq;
    write_u16_le(request + 6, payload_len);
    if (payload_len > 0u && payload != NULL) {
        memcpy(request + PICOMSO_PACKET_HEADER_SIZE, payload, payload_len);
    }

    wire_length = (uint16_t)(PICOMSO_PACKET_HEADER_SIZE + payload_len);
    if (proto->transport.control_write(proto->transport.user_data, request, wire_length) != 0) {
        return PICOMSO_RESULT_ERR_IO;
    }
    if (proto->transport.bulk_read(proto->transport.user_data, response, PICOMSO_PROTOCOL_IO_BUFFER_SIZE, &actual_length) != 0) {
        return PICOMSO_RESULT_ERR_IO;
    }

    if (actual_length < PICOMSO_PACKET_HEADER_SIZE) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }
    if (read_u16_le(response) != PICOMSO_PACKET_MAGIC) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }
    if (response[2] != PICOMSO_PROTOCOL_VERSION_MAJOR) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }
    if (response[5] != seq) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    *response_len = actual_length;
    wire_length = read_u16_le(response + 6);
    if ((size_t)PICOMSO_PACKET_HEADER_SIZE + wire_length > actual_length) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    if (response[4] == PICOMSO_MSG_ERROR) {
        const uint8_t *payload_ptr = response + PICOMSO_PACKET_HEADER_SIZE;
        uint8_t msg_len;

        if (wire_length < 2u) {
            return PICOMSO_RESULT_ERR_PROTOCOL;
        }

        proto->last_device_status = payload_ptr[0];
        msg_len = payload_ptr[1];
        if ((uint16_t)(2u + msg_len) > wire_length) {
            return PICOMSO_RESULT_ERR_PROTOCOL;
        }
        set_error_text(proto, (const char *)(payload_ptr + 2), msg_len);
        return PICOMSO_RESULT_ERR_DEVICE;
    }

    if (response[4] != expected_response_type) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    return PICOMSO_RESULT_OK;
}

static picomso_result_t parse_ack_status(const uint8_t *response, size_t response_len)
{
    size_t payload_len;
    const uint8_t *payload;

    if (response == NULL || response_len < PICOMSO_PACKET_HEADER_SIZE) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    payload_len = read_u16_le(response + 6);
    payload = response + PICOMSO_PACKET_HEADER_SIZE;
    if (payload_len == 0u) {
        return PICOMSO_RESULT_OK;
    }
    if (payload_len < 1u || response_len < PICOMSO_PACKET_HEADER_SIZE + payload_len) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }
    if (payload[0] != (uint8_t)PICOMSO_STATUS_OK) {
        return PICOMSO_RESULT_ERR_DEVICE;
    }

    return PICOMSO_RESULT_OK;
}

void picomso_protocol_init(picomso_protocol_t *proto, const picomso_transport_t *transport)
{
    if (proto == NULL) {
        return;
    }

    memset(proto, 0, sizeof(*proto));
    if (transport != NULL) {
        proto->transport = *transport;
    }
    proto->next_seq = 1u;
    clear_error_state(proto);
}

picomso_result_t picomso_protocol_get_info(picomso_protocol_t *proto, picomso_info_response_t *info)
{
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len = 0u;
    size_t payload_len;
    const uint8_t *payload;
    picomso_result_t result;

    if (info == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }

    result = send_request(proto, PICOMSO_MSG_GET_INFO, NULL, 0u, PICOMSO_RESPONSE_TYPE_INFO, response, &response_len);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    payload_len = read_u16_le(response + 6);
    if (payload_len < sizeof(*info)) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    payload = response + PICOMSO_PACKET_HEADER_SIZE;
    info->protocol_version_major = payload[0];
    info->protocol_version_minor = payload[1];
    memset(info->fw_id, 0, sizeof(info->fw_id));
    memcpy(info->fw_id, payload + 2, sizeof(info->fw_id));
    info->fw_id[sizeof(info->fw_id) - 1u] = '\0';

    return PICOMSO_RESULT_OK;
}

picomso_result_t picomso_protocol_get_capabilities(picomso_protocol_t *proto, uint32_t *capabilities)
{
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len = 0u;
    const uint8_t *payload;
    picomso_result_t result;

    if (capabilities == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }

    result = send_request(proto,
                          PICOMSO_MSG_GET_CAPABILITIES,
                          NULL,
                          0u,
                          PICOMSO_RESPONSE_TYPE_CAPABILITIES,
                          response,
                          &response_len);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    if (read_u16_le(response + 6) < 4u) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    payload = response + PICOMSO_PACKET_HEADER_SIZE;
    *capabilities = read_u32_le(payload);
    return PICOMSO_RESULT_OK;
}

picomso_result_t picomso_protocol_get_status(picomso_protocol_t *proto, picomso_status_response_t *status)
{
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len = 0u;
    const uint8_t *payload;
    picomso_result_t result;

    if (status == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }

    result = send_request(proto, PICOMSO_MSG_GET_STATUS, NULL, 0u, PICOMSO_RESPONSE_TYPE_STATUS, response, &response_len);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    if (read_u16_le(response + 6) < 2u) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    payload = response + PICOMSO_PACKET_HEADER_SIZE;
    status->mode = payload[0];
    status->capture_state = payload[1];
    return PICOMSO_RESULT_OK;
}

picomso_result_t picomso_protocol_set_mode(picomso_protocol_t *proto, picomso_device_mode_t mode)
{
    uint8_t payload[1];
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len = 0u;
    picomso_result_t result;

    payload[0] = (uint8_t)mode;
    result = send_request(proto, PICOMSO_MSG_SET_MODE, payload, sizeof(payload), PICOMSO_RESPONSE_TYPE_SET_MODE, response, &response_len);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    return parse_ack_status(response, response_len);
}

picomso_result_t picomso_protocol_request_capture(picomso_protocol_t *proto,
                                                  const picomso_request_capture_request_t *request)
{
    uint8_t payload[sizeof(*request)];
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len = 0u;
    unsigned int i;
    picomso_result_t result;

    if (request == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }

    write_u32_le(payload, request->total_samples);
    write_u32_le(payload + 4, request->rate);
    write_u32_le(payload + 8, request->pre_trigger_samples);
    for (i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
        size_t offset = 12u + (size_t)i * 3u;
        payload[offset] = request->trigger[i].is_enabled;
        payload[offset + 1u] = request->trigger[i].pin;
        payload[offset + 2u] = request->trigger[i].match;
    }

    result = send_request(proto,
                          PICOMSO_MSG_REQUEST_CAPTURE,
                          payload,
                          (uint16_t)sizeof(payload),
                          PICOMSO_RESPONSE_TYPE_REQUEST,
                          response,
                          &response_len);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    return parse_ack_status(response, response_len);
}

picomso_result_t picomso_protocol_read_data_block(picomso_protocol_t *proto, picomso_logic_block_t *block)
{
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len = 0u;
    uint16_t payload_len;
    const uint8_t *payload;
    picomso_result_t result;

    if (block == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }

    memset(block, 0, sizeof(*block));
    result = send_request(proto,
                          PICOMSO_MSG_READ_DATA_BLOCK,
                          NULL,
                          0u,
                          PICOMSO_RESPONSE_TYPE_DATA_BLOCK,
                          response,
                          &response_len);
    if (result == PICOMSO_RESULT_ERR_DEVICE &&
        proto->last_device_status == (uint8_t)PICOMSO_STATUS_ERR_UNKNOWN &&
        strcmp(proto->last_error_text, "no finalized capture data") == 0) {
        return PICOMSO_RESULT_DONE;
    }
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    payload_len = read_u16_le(response + 6);
    if (payload_len < 4u) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    payload = response + PICOMSO_PACKET_HEADER_SIZE;
    block->block_id = read_u16_le(payload);
    block->data_len = read_u16_le(payload + 2);
    if (block->data_len > PICOMSO_DATA_BLOCK_SIZE) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }
    if ((uint16_t)(4u + block->data_len) > payload_len) {
        return PICOMSO_RESULT_ERR_PROTOCOL;
    }

    if (block->data_len > 0u) {
        memcpy(block->data, payload + 4, block->data_len);
    }

    (void)response_len;
    return PICOMSO_RESULT_OK;
}
