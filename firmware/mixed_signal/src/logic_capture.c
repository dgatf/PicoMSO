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

#include "logic_capture.h"

#include "hardware/gpio.h"

#include <string.h>

#define LOGIC_CAPTURE_CHANNELS              16u
#define LOGIC_CAPTURE_TRIGGER_TIMEOUT_SPINS 4096u
#define LOGIC_CAPTURE_TRIGGER_COUNT         4u

typedef enum logic_capture_phase_t {
    LOGIC_CAPTURE_PHASE_DISARMED = 0,
    LOGIC_CAPTURE_PHASE_CAPTURING,
    LOGIC_CAPTURE_PHASE_FINALIZED
} logic_capture_phase_t;

static capture_config_t s_logic_capture_config = {
    .total_samples = 0u,
    .rate = 0u,
    .pre_trigger_samples = 0u,
    .channels = LOGIC_CAPTURE_CHANNELS,
    .trigger = {
        {
            .is_enabled = true,
            .pin = 0u,
            .match = TRIGGER_TYPE_EDGE_HIGH
        },
        { .is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW },
        { .is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW },
        { .is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW },
    }
};

static logic_capture_phase_t s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
static uint16_t s_capture_words[LOGIC_CAPTURE_MAX_SAMPLES];
static uint32_t s_capture_read_offset_bytes = 0u;

static uint16_t logic_capture_read_sample(void);
static bool logic_capture_triggered(uint16_t prev_sample, uint16_t sample);
static bool logic_capture_trigger_match(trigger_t trigger, uint16_t prev_sample, uint16_t sample);
static void logic_capture_configure_inputs(void);
static void logic_capture_reverse_range(uint16_t *buf, uint32_t start, uint32_t end);
static void logic_capture_rotate_left(uint16_t *buf, uint32_t length, uint32_t shift);

void logic_capture_reset(void)
{
    s_logic_capture_config.total_samples = 0u;
    s_logic_capture_config.pre_trigger_samples = 0u;
    s_capture_read_offset_bytes = 0u;
    s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
}

bool logic_capture_start(const capture_config_t *config)
{
    uint32_t pre_trigger_count;
    uint32_t post_trigger_count;
    uint16_t prev_sample;
    uint16_t trigger_sample;
    uint32_t ring_head = 0u;
    uint32_t ring_count = 0u;
    bool triggered = false;

    if (config == NULL) {
        return false;
    }

    if (config->total_samples == 0u || config->total_samples > LOGIC_CAPTURE_MAX_SAMPLES) {
        return false;
    }

    if (config->pre_trigger_samples > config->total_samples) {
        return false;
    }

    s_logic_capture_config = *config;
    s_logic_capture_config.channels = LOGIC_CAPTURE_CHANNELS;
    s_capture_read_offset_bytes = 0u;

    logic_capture_configure_inputs();
    s_phase = LOGIC_CAPTURE_PHASE_CAPTURING;

    pre_trigger_count = s_logic_capture_config.pre_trigger_samples;
    post_trigger_count = s_logic_capture_config.total_samples - pre_trigger_count;

    prev_sample = logic_capture_read_sample();
    trigger_sample = prev_sample;

    for (uint32_t spin = 0u; spin < LOGIC_CAPTURE_TRIGGER_TIMEOUT_SPINS; ++spin) {
        const uint16_t sample = logic_capture_read_sample();

        if (logic_capture_triggered(prev_sample, sample)) {
            trigger_sample = sample;
            triggered = true;
            break;
        }

        if (pre_trigger_count > 0u) {
            s_capture_words[ring_head] = sample;
            ring_head = (ring_head + 1u) % pre_trigger_count;
            if (ring_count < pre_trigger_count) {
                ++ring_count;
            }
        }

        prev_sample = sample;
    }

    if (!triggered && post_trigger_count > 0u) {
        /* Complete the requested one-shot capture even if no trigger edge
         * arrives by using the current GPIO snapshot as the first post-trigger
         * sample stored in the finalized capture buffer. */
        trigger_sample = logic_capture_read_sample();
    }

    if (pre_trigger_count > 0u) {
        if (ring_count == pre_trigger_count && ring_head != 0u) {
            logic_capture_rotate_left(s_capture_words, pre_trigger_count, ring_head);
        } else if (ring_count < pre_trigger_count) {
            const uint32_t missing_pre_trigger = pre_trigger_count - ring_count;
            memmove(&s_capture_words[missing_pre_trigger], &s_capture_words[0], ring_count * sizeof(uint16_t));
            memset(&s_capture_words[0], 0, missing_pre_trigger * sizeof(uint16_t));
        }
    }

    if (post_trigger_count > 0u) {
        s_capture_words[pre_trigger_count] = trigger_sample;
        for (uint32_t i = 1u; i < post_trigger_count; ++i) {
            s_capture_words[pre_trigger_count + i] = logic_capture_read_sample();
        }
    }

    s_phase = LOGIC_CAPTURE_PHASE_FINALIZED;
    return true;
}

capture_state_t logic_capture_get_state(void)
{
    if (s_phase == LOGIC_CAPTURE_PHASE_CAPTURING) {
        return CAPTURE_RUNNING;
    }

    return CAPTURE_IDLE;
}

bool logic_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len)
{
    const uint32_t total_bytes = s_logic_capture_config.total_samples * sizeof(uint16_t);
    uint32_t remaining_bytes;
    uint16_t chunk_bytes;

    if (block_id == NULL || data == NULL || data_len == NULL) {
        return false;
    }

    if (s_phase != LOGIC_CAPTURE_PHASE_FINALIZED) {
        return false;
    }

    if (s_capture_read_offset_bytes >= total_bytes) {
        return false;
    }

    remaining_bytes = total_bytes - s_capture_read_offset_bytes;
    chunk_bytes =
        (remaining_bytes > LOGIC_CAPTURE_BLOCK_BYTES) ? LOGIC_CAPTURE_BLOCK_BYTES : (uint16_t)remaining_bytes;

    memcpy(data, ((const uint8_t *)s_capture_words) + s_capture_read_offset_bytes, chunk_bytes);
    *block_id = (uint16_t)(s_capture_read_offset_bytes / LOGIC_CAPTURE_BLOCK_BYTES);
    *data_len = chunk_bytes;
    s_capture_read_offset_bytes += chunk_bytes;

    return true;
}

static uint16_t logic_capture_read_sample(void)
{
    const uint32_t pin_mask = (UINT32_C(1) << LOGIC_CAPTURE_CHANNELS) - 1u;
    return (uint16_t)(gpio_get_all() & pin_mask);
}

static bool logic_capture_triggered(uint16_t prev_sample, uint16_t sample)
{
    for (uint32_t i = 0u; i < LOGIC_CAPTURE_TRIGGER_COUNT; ++i) {
        if (!s_logic_capture_config.trigger[i].is_enabled) {
            continue;
        }

        if (logic_capture_trigger_match(s_logic_capture_config.trigger[i], prev_sample, sample)) {
            return true;
        }
    }

    return false;
}

static bool logic_capture_trigger_match(trigger_t trigger, uint16_t prev_sample, uint16_t sample)
{
    const uint16_t mask = (uint16_t)(UINT16_C(1) << trigger.pin);
    const bool prev_high = (prev_sample & mask) != 0u;
    const bool sample_high = (sample & mask) != 0u;

    switch (trigger.match) {
        case TRIGGER_TYPE_LEVEL_LOW:
            return !sample_high;

        case TRIGGER_TYPE_LEVEL_HIGH:
            return sample_high;

        case TRIGGER_TYPE_EDGE_LOW:
            return prev_high && !sample_high;

        case TRIGGER_TYPE_EDGE_HIGH:
            return !prev_high && sample_high;

        default:
            return false;
    }
}

static void logic_capture_configure_inputs(void)
{
    for (uint pin = 0u; pin < LOGIC_CAPTURE_CHANNELS; ++pin) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_pull_down(pin);
    }
}

static void logic_capture_reverse_range(uint16_t *buf, uint32_t start, uint32_t end)
{
    while (start < end) {
        const uint16_t tmp = buf[start];
        buf[start] = buf[end];
        buf[end] = tmp;
        ++start;
        --end;
    }
}

static void logic_capture_rotate_left(uint16_t *buf, uint32_t length, uint32_t shift)
{
    if (length == 0u) {
        return;
    }

    shift %= length;
    if (shift == 0u) {
        return;
    }

    logic_capture_reverse_range(buf, 0u, shift - 1u);
    logic_capture_reverse_range(buf, shift, length - 1u);
    logic_capture_reverse_range(buf, 0u, length - 1u);
}
