/*
 * PicoMSO - libsigrok-style host driver API layer
 * Copyright (C) 2026
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "api.h"

#include <string.h>

static const char *const logic_channel_names[PICOMSO_DRIVER_CHANNEL_COUNT] = {
    "D0",  "D1",  "D2",  "D3",  "D4",  "D5",  "D6",  "D7",
    "D8",  "D9",  "D10", "D11", "D12", "D13", "D14", "D15",
};

static bool request_is_valid(const picomso_request_capture_request_t *request)
{
    unsigned int i;

    if (request == NULL) {
        return false;
    }
    if (request->total_samples == 0u || request->pre_trigger_samples > request->total_samples) {
        return false;
    }

    for (i = 0u; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; ++i) {
        if (request->trigger[i].is_enabled > 1u) {
            return false;
        }
        if (request->trigger[i].pin >= PICOMSO_DRIVER_CHANNEL_COUNT) {
            return false;
        }
        switch ((picomso_trigger_match_t)request->trigger[i].match) {
            case PICOMSO_TRIGGER_MATCH_LEVEL_LOW:
            case PICOMSO_TRIGGER_MATCH_LEVEL_HIGH:
            case PICOMSO_TRIGGER_MATCH_EDGE_LOW:
            case PICOMSO_TRIGGER_MATCH_EDGE_HIGH:
                break;
            default:
                return false;
        }
    }

    return true;
}

void picomso_driver_init(picomso_driver_t *driver, const picomso_transport_t *transport)
{
    if (driver == NULL) {
        return;
    }

    memset(driver, 0, sizeof(*driver));
    picomso_protocol_init(&driver->protocol, transport);
    driver->channel_count = PICOMSO_DRIVER_CHANNEL_COUNT;
}

picomso_result_t picomso_driver_open(picomso_driver_t *driver)
{
    picomso_result_t result;

    if (driver == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }

    result = picomso_protocol_get_info(&driver->protocol, &driver->info);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    result = picomso_protocol_get_capabilities(&driver->protocol, &driver->capabilities);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }
    if ((driver->capabilities & PICOMSO_CAP_LOGIC) == 0u) {
        return PICOMSO_RESULT_ERR_UNSUPPORTED;
    }

    driver->is_open = true;
    driver->logic_mode_active = false;
    driver->capture_running = false;
    return PICOMSO_RESULT_OK;
}

picomso_result_t picomso_driver_stop(picomso_driver_t *driver)
{
    picomso_result_t result;

    if (driver == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }
    if (!driver->is_open) {
        return PICOMSO_RESULT_OK;
    }

    result = picomso_protocol_set_mode(&driver->protocol, PICOMSO_MODE_UNSET);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    driver->logic_mode_active = false;
    driver->capture_running = false;
    return PICOMSO_RESULT_OK;
}

picomso_result_t picomso_driver_close(picomso_driver_t *driver)
{
    picomso_result_t result;

    if (driver == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }
    if (!driver->is_open) {
        return PICOMSO_RESULT_OK;
    }

    result = picomso_driver_stop(driver);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    driver->is_open = false;
    return PICOMSO_RESULT_OK;
}

picomso_result_t picomso_driver_start_logic_capture(picomso_driver_t *driver,
                                                    const picomso_request_capture_request_t *request)
{
    picomso_status_response_t status;
    picomso_result_t result;

    if (driver == NULL || !request_is_valid(request)) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }
    if (!driver->is_open) {
        return PICOMSO_RESULT_ERR_STATE;
    }

    result = picomso_protocol_set_mode(&driver->protocol, PICOMSO_MODE_LOGIC);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    result = picomso_protocol_get_status(&driver->protocol, &status);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }
    if (status.mode != PICOMSO_MODE_LOGIC || status.capture_state == PICOMSO_CAPTURE_RUNNING) {
        return PICOMSO_RESULT_ERR_STATE;
    }

    result = picomso_protocol_request_capture(&driver->protocol, request);
    if (result != PICOMSO_RESULT_OK) {
        return result;
    }

    driver->logic_mode_active = true;
    driver->capture_running = true;
    return PICOMSO_RESULT_OK;
}

picomso_result_t picomso_driver_wait_capture_complete(picomso_driver_t *driver,
                                                      unsigned int max_polls,
                                                      unsigned int poll_interval_ms)
{
    picomso_status_response_t status;
    picomso_result_t result;
    unsigned int poll;

    if (driver == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }
    if (!driver->is_open || !driver->logic_mode_active || !driver->capture_running) {
        return PICOMSO_RESULT_ERR_STATE;
    }

    for (poll = 0u; poll < max_polls; ++poll) {
        result = picomso_protocol_get_status(&driver->protocol, &status);
        if (result != PICOMSO_RESULT_OK) {
            return result;
        }
        if (status.mode != PICOMSO_MODE_LOGIC) {
            return PICOMSO_RESULT_ERR_STATE;
        }
        if (status.capture_state == PICOMSO_CAPTURE_IDLE) {
            driver->capture_running = false;
            return PICOMSO_RESULT_OK;
        }
        if (status.capture_state != PICOMSO_CAPTURE_RUNNING) {
            return PICOMSO_RESULT_ERR_PROTOCOL;
        }
        if (poll_interval_ms > 0u && driver->protocol.transport.wait_ms != NULL && poll + 1u < max_polls) {
            if (driver->protocol.transport.wait_ms(driver->protocol.transport.user_data, poll_interval_ms) != 0) {
                return PICOMSO_RESULT_ERR_IO;
            }
        }
    }

    return PICOMSO_RESULT_ERR_TIMEOUT;
}

picomso_result_t picomso_driver_read_logic_capture(picomso_driver_t *driver,
                                                   picomso_logic_samples_cb callback,
                                                   void *user_data,
                                                   size_t *captured_samples)
{
    picomso_logic_block_t block;
    uint16_t samples[PICOMSO_DATA_BLOCK_SIZE / sizeof(uint16_t)];
    uint16_t expected_block_id = 0u;
    size_t total_samples = 0u;
    size_t sample_count;
    size_t i;
    picomso_result_t result;

    if (driver == NULL || callback == NULL) {
        return PICOMSO_RESULT_ERR_ARGUMENT;
    }
    if (!driver->is_open || !driver->logic_mode_active || driver->capture_running) {
        return PICOMSO_RESULT_ERR_STATE;
    }

    for (;;) {
        result = picomso_protocol_read_data_block(&driver->protocol, &block);
        if (result == PICOMSO_RESULT_DONE) {
            if (captured_samples != NULL) {
                *captured_samples = total_samples;
            }
            return PICOMSO_RESULT_OK;
        }
        if (result != PICOMSO_RESULT_OK) {
            return result;
        }
        if (block.block_id != expected_block_id) {
            return PICOMSO_RESULT_ERR_PROTOCOL;
        }
        if ((block.data_len % sizeof(uint16_t)) != 0u) {
            return PICOMSO_RESULT_ERR_PROTOCOL;
        }

        sample_count = block.data_len / sizeof(uint16_t);
        for (i = 0u; i < sample_count; ++i) {
            samples[i] = (uint16_t)block.data[i * 2u] | ((uint16_t)block.data[i * 2u + 1u] << 8);
        }
        if (sample_count > 0u && callback(user_data, samples, sample_count) != 0) {
            return PICOMSO_RESULT_ERR_CALLBACK;
        }

        total_samples += sample_count;
        ++expected_block_id;
    }
}

const char *picomso_driver_logic_channel_name(unsigned int index)
{
    if (index >= PICOMSO_DRIVER_CHANNEL_COUNT) {
        return NULL;
    }

    return logic_channel_names[index];
}
