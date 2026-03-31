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

#ifndef PICOMSO_SIGROK_API_H
#define PICOMSO_SIGROK_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "firmware/protocol/include/protocol_packets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PICOMSO_SIGROK_ERROR_TEXT_MAX  128u
#define PICOMSO_SIGROK_FW_ID_MAX       PICOMSO_INFO_FW_ID_MAX

typedef enum {
    PICOMSO_SIGROK_OK = 0,
    PICOMSO_SIGROK_ERR_ARG,
    PICOMSO_SIGROK_ERR_TRANSPORT,
    PICOMSO_SIGROK_ERR_PROTOCOL,
    PICOMSO_SIGROK_ERR_DEVICE,
    PICOMSO_SIGROK_ERR_UNSUPPORTED,
    PICOMSO_SIGROK_ERR_TIMEOUT,
    PICOMSO_SIGROK_ERR_CALLBACK,
} picomso_sigrok_status_t;

typedef struct {
    bool (*send_control)(void *user_data, const uint8_t *buf, size_t len);
    bool (*recv_bulk)(void *user_data, uint8_t *buf, size_t buf_capacity, size_t *received_len);
    void (*sleep_ms)(void *user_data, uint32_t milliseconds);
} picomso_sigrok_transport_ops_t;

typedef struct {
    uint32_t total_samples;
    uint32_t sample_rate_hz;
    uint32_t pre_trigger_samples;
    picomso_trigger_config_t triggers[PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT];
    uint32_t poll_interval_ms;
    uint32_t poll_timeout_ms;
} picomso_sigrok_logic_config_t;

typedef bool (*picomso_sigrok_samples_cb_t)(void *user_data,
                                            const uint16_t *samples,
                                            size_t sample_count);

typedef struct {
    const picomso_sigrok_transport_ops_t *transport_ops;
    void                                 *transport_data;
    uint8_t                               next_seq;
    uint16_t                              next_block_id;
    bool                                  is_open;
    bool                                  acquisition_running;
    uint8_t                               protocol_version_major;
    uint8_t                               protocol_version_minor;
    uint32_t                              capabilities;
    char                                  firmware_id[PICOMSO_SIGROK_FW_ID_MAX];
    char                                  last_error[PICOMSO_SIGROK_ERROR_TEXT_MAX];
} picomso_sigrok_driver_t;

void picomso_sigrok_driver_init(picomso_sigrok_driver_t *driver,
                                const picomso_sigrok_transport_ops_t *transport_ops,
                                void *transport_data);

picomso_sigrok_status_t picomso_sigrok_dev_open(picomso_sigrok_driver_t *driver);
picomso_sigrok_status_t picomso_sigrok_dev_close(picomso_sigrok_driver_t *driver);
picomso_sigrok_status_t picomso_sigrok_acquisition_start(picomso_sigrok_driver_t *driver,
                                                         const picomso_sigrok_logic_config_t *config,
                                                         picomso_sigrok_samples_cb_t samples_cb,
                                                         void *samples_cb_data,
                                                         size_t *captured_samples_out);
picomso_sigrok_status_t picomso_sigrok_acquisition_stop(picomso_sigrok_driver_t *driver);

const char *picomso_sigrok_last_error(const picomso_sigrok_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_SIGROK_API_H */
