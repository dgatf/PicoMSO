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

#ifndef PICOMSO_TYPES_H
#define PICOMSO_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "pico/types.h"

// Generic capture state shared across both firmware projects
typedef enum capture_state_t {
    CAPTURE_IDLE,
    CAPTURE_RUNNING
} capture_state_t;

// Trigger match type (level or edge, high or low)
typedef enum trigger_match_t {
    TRIGGER_TYPE_LEVEL_LOW,
    TRIGGER_TYPE_LEVEL_HIGH,
    TRIGGER_TYPE_EDGE_LOW,
    TRIGGER_TYPE_EDGE_HIGH
} trigger_match_t;

// Single-channel trigger configuration
typedef struct trigger_t {
    bool is_enabled;
    uint pin;
    trigger_match_t match;
} trigger_t;

// Logic-analyzer capture configuration.
// Oscilloscope uses oscilloscope_config_t for its ADC-specific fields.
typedef struct capture_config_t {
    uint total_samples;
    uint rate;
    uint pre_trigger_samples;
    uint channels;
    trigger_t trigger[4];
} capture_config_t;

typedef struct capture_trigger_gate_t {
    // When enabled, mixed scope startup waits on an external trigger gate rather
    // than starting its post-trigger path immediately. dreq is the fully
    // prepared hardware DREQ source that signals that gate.
    bool enabled;
    uint dreq;
} capture_trigger_gate_t;

typedef enum coupling_t { COUPLING_DC, COUPLING_AC } coupling_t;
typedef enum channel_t { CHANNEL1, CHANNEL2 } channel_t;
typedef enum gpio_config_t { GPIO_DEBUG_ENABLE = 18, GPIO_NO_CONVERSION = 19 } gpio_config_t;
typedef void (*complete_handler_t)(void);

#ifdef __cplusplus
}
#endif

#endif
