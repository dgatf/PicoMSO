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

#ifndef PICOMSO_LOGIC_CAPTURE_H
#define PICOMSO_LOGIC_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define LOGIC_CAPTURE_BLOCK_BYTES  64u
#define LOGIC_CAPTURE_MAX_SAMPLES  100000u

typedef void (*complete_handler_t)(void);

void logic_capture_reset(void);
bool logic_capture_prestart(const capture_config_t *config, complete_handler_t handler);
bool logic_capture_start(const capture_config_t *config, complete_handler_t handler);
void logic_capture_commit_start(void);
capture_state_t logic_capture_get_state(void);
bool logic_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len);
uint logic_capture_get_sm_mux(void);
uint logic_capture_get_trigger_count(void);

#ifdef __cplusplus
}
#endif

#endif
