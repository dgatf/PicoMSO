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

#include "scope_capture.h"

#include "hardware/adc.h"

#include <string.h>

#define SCOPE_CAPTURE_ADC_GPIO   26u
#define SCOPE_CAPTURE_ADC_INPUT  0u

typedef enum scope_capture_phase_t {
    SCOPE_CAPTURE_PHASE_DISARMED = 0,
    SCOPE_CAPTURE_PHASE_CAPTURING,
    SCOPE_CAPTURE_PHASE_FINALIZED
} scope_capture_phase_t;

static capture_config_t s_scope_capture_config = {
    .total_samples = 0u,
    .rate = 0u,
    .pre_trigger_samples = 0u,
    .channels = 1u,
    .trigger = {
        { .is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW },
        { .is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW },
        { .is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW },
        { .is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW },
    }
};

static scope_capture_phase_t s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
static uint16_t s_capture_samples[SCOPE_CAPTURE_MAX_SAMPLES];
static uint32_t s_capture_read_offset_bytes = 0u;

static void scope_capture_configure_adc(void);

void scope_capture_reset(void)
{
    s_scope_capture_config.total_samples = 0u;
    s_scope_capture_config.pre_trigger_samples = 0u;
    s_capture_read_offset_bytes = 0u;
    s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
}

bool scope_capture_start(const capture_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (config->total_samples == 0u || config->total_samples > SCOPE_CAPTURE_MAX_SAMPLES) {
        return false;
    }

    if (config->pre_trigger_samples > config->total_samples) {
        return false;
    }

    s_scope_capture_config = *config;
    s_scope_capture_config.channels = 1u;
    s_capture_read_offset_bytes = 0u;

    scope_capture_configure_adc();
    s_phase = SCOPE_CAPTURE_PHASE_CAPTURING;

    for (uint32_t i = 0u; i < s_scope_capture_config.total_samples; ++i) {
        s_capture_samples[i] = adc_read();
    }

    s_phase = SCOPE_CAPTURE_PHASE_FINALIZED;
    return true;
}

capture_state_t scope_capture_get_state(void)
{
    if (s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
        return CAPTURE_RUNNING;
    }

    return CAPTURE_IDLE;
}

bool scope_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len)
{
    const uint32_t total_bytes = s_scope_capture_config.total_samples * sizeof(uint16_t);
    uint32_t remaining_bytes;
    uint16_t chunk_bytes;

    if (block_id == NULL || data == NULL || data_len == NULL) {
        return false;
    }

    if (s_phase != SCOPE_CAPTURE_PHASE_FINALIZED) {
        return false;
    }

    if (s_capture_read_offset_bytes >= total_bytes) {
        return false;
    }

    remaining_bytes = total_bytes - s_capture_read_offset_bytes;
    chunk_bytes =
        (remaining_bytes > SCOPE_CAPTURE_BLOCK_BYTES) ? SCOPE_CAPTURE_BLOCK_BYTES : (uint16_t)remaining_bytes;

    memcpy(data, ((const uint8_t *)s_capture_samples) + s_capture_read_offset_bytes, chunk_bytes);
    *block_id = (uint16_t)(s_capture_read_offset_bytes / SCOPE_CAPTURE_BLOCK_BYTES);
    *data_len = chunk_bytes;
    s_capture_read_offset_bytes += chunk_bytes;

    return true;
}

static void scope_capture_configure_adc(void)
{
    adc_init();
    adc_gpio_init(SCOPE_CAPTURE_ADC_GPIO);
    adc_select_input(SCOPE_CAPTURE_ADC_INPUT);
}
