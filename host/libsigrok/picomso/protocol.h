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

#ifndef PICOMSO_SIGROK_PROTOCOL_H
#define PICOMSO_SIGROK_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "firmware/protocol/include/protocol.h"
#include "firmware/protocol/include/protocol_packets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PICOMSO_SIGROK_PACKET_BUF_SIZE  256u
#define PICOMSO_SIGROK_ERROR_TEXT_MAX   128u

typedef struct {
    picomso_packet_header_t header;
    const uint8_t          *payload;
} picomso_sigrok_response_t;

typedef struct {
    picomso_status_t status;
    char             message[PICOMSO_SIGROK_ERROR_TEXT_MAX];
} picomso_sigrok_device_error_t;

size_t picomso_sigrok_build_request(uint8_t *buf,
                                    size_t buf_len,
                                    picomso_msg_type_t msg_type,
                                    uint8_t seq,
                                    const void *payload,
                                    size_t payload_len);

bool picomso_sigrok_parse_response(const uint8_t *buf,
                                   size_t buf_len,
                                   uint8_t expected_seq,
                                   picomso_sigrok_response_t *response,
                                   char *error_text,
                                   size_t error_text_len);

bool picomso_sigrok_decode_device_error(const picomso_sigrok_response_t *response,
                                        picomso_sigrok_device_error_t *device_error,
                                        char *error_text,
                                        size_t error_text_len);

bool picomso_sigrok_decode_info(const picomso_sigrok_response_t *response,
                                picomso_info_response_t *info,
                                char *error_text,
                                size_t error_text_len);

bool picomso_sigrok_decode_capabilities(const picomso_sigrok_response_t *response,
                                        picomso_capabilities_response_t *capabilities,
                                        char *error_text,
                                        size_t error_text_len);

bool picomso_sigrok_decode_status(const picomso_sigrok_response_t *response,
                                  picomso_status_response_t *status,
                                  char *error_text,
                                  size_t error_text_len);

bool picomso_sigrok_decode_data_block(const picomso_sigrok_response_t *response,
                                      uint16_t *block_id,
                                      const uint8_t **data,
                                      uint16_t *data_len,
                                      char *error_text,
                                      size_t error_text_len);

bool picomso_sigrok_device_error_is_end_of_capture(const picomso_sigrok_device_error_t *device_error);

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_SIGROK_PROTOCOL_H */
