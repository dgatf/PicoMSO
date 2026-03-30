/*
 * Logic Analyzer RP2040-SUMP
 * Copyright (C) 2023 Daniel Gorbea <danielgorbea@hotmail.com>
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

#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#include "debug.h"
#include "types.h"

// Number of channels
#define CHANNEL_COUNT 16

// Maximum number of triggers
#define TRIGGERS_COUNT 4

// Debug buffer size
#define DEBUG_BUFFER_SIZE 300

typedef enum gpio_config_t {
    GPIO_DEBUG_ENABLE = 18,
    GPIO_TRIGGER_STAGES = 19  // If gpio 20 grounded: triggers are based on stages. If gpio 20 not grounded: all
                              // triggers at stage 0 are edge triggers
} gpio_config_t;

typedef enum command_t { COMMAND_NONE, COMMAND_RESET, COMMAND_CAPTURE } command_t;

typedef struct config_t {
    uint channels;
    bool trigger_edge;
    bool debug;
} config_t;

#ifdef __cplusplus
}
#endif

#endif
