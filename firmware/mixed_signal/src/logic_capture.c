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

#define LOGIC_CAPTURE_CHANNELS             16u
#define LOGIC_CAPTURE_POST_TRIGGER_SAMPLES 15u
#define LOGIC_CAPTURE_TRIGGER_TIMEOUT_SPINS 4096u

#if LOGIC_CAPTURE_TOTAL_SAMPLES != ((LOGIC_CAPTURE_BLOCK_BYTES / sizeof(uint16_t)))
#error "logic capture block size must match the number of 16-bit samples"
#endif

typedef enum logic_capture_phase_t {
    LOGIC_CAPTURE_PHASE_DISARMED = 0,
    LOGIC_CAPTURE_PHASE_ARMED,
    LOGIC_CAPTURE_PHASE_TRIGGERED,
    LOGIC_CAPTURE_PHASE_FINALIZED
} logic_capture_phase_t;

static const capture_config_t s_logic_capture_config = {
    .total_samples = LOGIC_CAPTURE_TOTAL_SAMPLES,
    .rate = 0,
    .pre_trigger_samples = LOGIC_CAPTURE_PRE_TRIGGER_SAMPLES,
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
static uint8_t s_capture_block[LOGIC_CAPTURE_BLOCK_BYTES];
static uint8_t s_capture_id = 0u;
static uint8_t s_current_block_id = 0u;

static uint16_t logic_capture_read_sample(void);
static bool logic_capture_triggered(uint16_t prev_sample, uint16_t sample);
static bool logic_capture_trigger_match(trigger_t trigger, uint16_t prev_sample, uint16_t sample);
static void logic_capture_configure_inputs(void);
static void logic_capture_finalize(void);

void logic_capture_reset(void)
{
    s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
}

void logic_capture_arm(void)
{
    logic_capture_configure_inputs();
    s_phase = LOGIC_CAPTURE_PHASE_ARMED;
}

capture_state_t logic_capture_get_state(void)
{
    switch (s_phase) {
        case LOGIC_CAPTURE_PHASE_ARMED:
        case LOGIC_CAPTURE_PHASE_TRIGGERED:
            return CAPTURE_RUNNING;

        case LOGIC_CAPTURE_PHASE_DISARMED:
        case LOGIC_CAPTURE_PHASE_FINALIZED:
        default:
            return CAPTURE_IDLE;
    }
}

bool logic_capture_read_block(uint8_t *block_id, uint8_t *data, uint16_t *data_len)
{
    if (block_id == NULL || data == NULL || data_len == NULL) {
        return false;
    }

    if (s_phase == LOGIC_CAPTURE_PHASE_DISARMED) {
        return false;
    }

    if (s_phase != LOGIC_CAPTURE_PHASE_FINALIZED) {
        logic_capture_finalize();
    }

    memcpy(data, s_capture_block, sizeof(s_capture_block));
    *block_id = s_current_block_id;
    *data_len = (uint16_t)sizeof(s_capture_block);
    return true;
}

static uint16_t logic_capture_read_sample(void)
{
    const uint32_t pin_mask = (UINT32_C(1) << s_logic_capture_config.channels) - 1u;
    return (uint16_t)(gpio_get_all() & pin_mask);
}

static bool logic_capture_triggered(uint16_t prev_sample, uint16_t sample)
{
    for (uint i = 0; i < 4u; ++i) {
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
    for (uint pin = 0u; pin < s_logic_capture_config.channels; ++pin) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_pull_down(pin);
    }
}

static void logic_capture_finalize(void)
{
    uint16_t pre_trigger_ring[LOGIC_CAPTURE_PRE_TRIGGER_SAMPLES];
    uint16_t capture_words[LOGIC_CAPTURE_TOTAL_SAMPLES];
    uint16_t prev_sample = logic_capture_read_sample();
    uint16_t trigger_sample = prev_sample;
    uint ring_head = 0u;
    uint ring_count = 0u;
    bool triggered = false;

    for (uint spin = 0u; spin < LOGIC_CAPTURE_TRIGGER_TIMEOUT_SPINS; ++spin) {
        const uint16_t sample = logic_capture_read_sample();

        if (logic_capture_triggered(prev_sample, sample)) {
            trigger_sample = sample;
            triggered = true;
            break;
        }

        pre_trigger_ring[ring_head] = sample;
        ring_head = (ring_head + 1u) % LOGIC_CAPTURE_PRE_TRIGGER_SAMPLES;
        if (ring_count < LOGIC_CAPTURE_PRE_TRIGGER_SAMPLES) {
            ++ring_count;
        }

        prev_sample = sample;
    }

    if (!triggered) {
        trigger_sample = logic_capture_read_sample();
    }

    s_phase = LOGIC_CAPTURE_PHASE_TRIGGERED;

    {
        const uint missing_pre_trigger = LOGIC_CAPTURE_PRE_TRIGGER_SAMPLES - ring_count;
        uint out_index = 0u;

        for (; out_index < missing_pre_trigger; ++out_index) {
            capture_words[out_index] = 0u;
        }

        for (uint i = 0u; i < ring_count; ++i, ++out_index) {
            const uint src_index = (ring_head + i) % LOGIC_CAPTURE_PRE_TRIGGER_SAMPLES;
            capture_words[out_index] = pre_trigger_ring[src_index];
        }

        capture_words[out_index++] = trigger_sample;

        for (uint i = 0u; i < LOGIC_CAPTURE_POST_TRIGGER_SAMPLES; ++i) {
            capture_words[out_index++] = logic_capture_read_sample();
        }
    }

    memcpy(s_capture_block, capture_words, sizeof(s_capture_block));
    s_current_block_id = s_capture_id++;
    s_phase = LOGIC_CAPTURE_PHASE_FINALIZED;
}
