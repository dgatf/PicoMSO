/*
 * PicoMSO - libsigrok-style host driver API layer
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef PICOMSO_SIGROK_API_H
#define PICOMSO_SIGROK_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*picomso_logic_samples_cb)(void *user_data, const uint16_t *samples, size_t sample_count);

typedef struct {
    picomso_protocol_t protocol;
    picomso_info_response_t info;
    uint32_t capabilities;
    unsigned int channel_count;
    bool is_open;
    bool logic_mode_active;
    bool capture_running;
} picomso_driver_t;

void picomso_driver_init(picomso_driver_t *driver, const picomso_transport_t *transport);
picomso_result_t picomso_driver_open(picomso_driver_t *driver);
picomso_result_t picomso_driver_close(picomso_driver_t *driver);
picomso_result_t picomso_driver_start_logic_capture(picomso_driver_t *driver,
                                                    const picomso_request_capture_request_t *request);
picomso_result_t picomso_driver_wait_capture_complete(picomso_driver_t *driver,
                                                      unsigned int max_polls,
                                                      unsigned int poll_interval_ms);
picomso_result_t picomso_driver_read_logic_capture(picomso_driver_t *driver,
                                                   picomso_logic_samples_cb callback,
                                                   void *user_data,
                                                   size_t *captured_samples);
picomso_result_t picomso_driver_stop(picomso_driver_t *driver);
const char *picomso_driver_logic_channel_name(unsigned int index);

#ifdef __cplusplus
}
#endif

#endif
