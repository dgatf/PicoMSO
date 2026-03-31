/*
 * PicoMSO - libsigrok-style host protocol layer
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef PICOMSO_SIGROK_PROTOCOL_H
#define PICOMSO_SIGROK_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../../firmware/protocol/include/protocol.h"
#include "../../../firmware/protocol/include/protocol_packets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PICOMSO_DRIVER_CHANNEL_COUNT 16u
#define PICOMSO_PROTOCOL_IO_BUFFER_SIZE 256u
#define PICOMSO_PROTOCOL_ERROR_TEXT_MAX 64u

typedef enum {
    PICOMSO_RESULT_OK = 0,
    PICOMSO_RESULT_DONE = 1,
    PICOMSO_RESULT_ERR_ARGUMENT = -1,
    PICOMSO_RESULT_ERR_IO = -2,
    PICOMSO_RESULT_ERR_PROTOCOL = -3,
    PICOMSO_RESULT_ERR_DEVICE = -4,
    PICOMSO_RESULT_ERR_UNSUPPORTED = -5,
    PICOMSO_RESULT_ERR_STATE = -6,
    PICOMSO_RESULT_ERR_CALLBACK = -7,
    PICOMSO_RESULT_ERR_TIMEOUT = -8,
} picomso_result_t;

typedef struct {
    int (*control_write)(void *user_data, const uint8_t *data, size_t length);
    int (*bulk_read)(void *user_data, uint8_t *data, size_t capacity, size_t *actual_length);
    int (*wait_ms)(void *user_data, unsigned int delay_ms);
    void *user_data;
} picomso_transport_t;

typedef struct {
    picomso_transport_t transport;
    uint8_t next_seq;
    uint8_t last_device_status;
    char last_error_text[PICOMSO_PROTOCOL_ERROR_TEXT_MAX];
} picomso_protocol_t;

typedef struct {
    uint16_t block_id;
    uint16_t data_len;
    uint8_t data[PICOMSO_DATA_BLOCK_SIZE];
} picomso_logic_block_t;

void picomso_protocol_init(picomso_protocol_t *proto, const picomso_transport_t *transport);

picomso_result_t picomso_protocol_get_info(picomso_protocol_t *proto, picomso_info_response_t *info);
picomso_result_t picomso_protocol_get_capabilities(picomso_protocol_t *proto, uint32_t *capabilities);
picomso_result_t picomso_protocol_get_status(picomso_protocol_t *proto, picomso_status_response_t *status);
picomso_result_t picomso_protocol_set_mode(picomso_protocol_t *proto, picomso_device_mode_t mode);
picomso_result_t picomso_protocol_request_capture(picomso_protocol_t *proto,
                                                  const picomso_request_capture_request_t *request);
picomso_result_t picomso_protocol_read_data_block(picomso_protocol_t *proto, picomso_logic_block_t *block);

#ifdef __cplusplus
}
#endif

#endif
