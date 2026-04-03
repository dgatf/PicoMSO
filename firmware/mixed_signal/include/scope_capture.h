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

#ifndef PICOMSO_SCOPE_CAPTURE_H
#define PICOMSO_SCOPE_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#define SCOPE_CAPTURE_BLOCK_BYTES 64u
#define SCOPE_CAPTURE_MAX_SAMPLES 50000u
#define SCOPE_CAPTURE_PRE_TRIGGER_MAX_SAMPLES 4096u

void scope_capture_reset(void);
bool scope_capture_start(const capture_config_t *config, complete_handler_t handler);
bool scope_capture_prestart(const capture_config_t *config, complete_handler_t handler);
void scope_capture_commit_start(void);
capture_state_t scope_capture_get_state(void);
bool scope_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len);
void oscilloscope_set_coupling(channel_t channel, coupling_t coupling);
void oscilloscope_set_samplerate(uint samplerate);
uint16_t scope_capture_get_sample_index(int index);
uint scope_capture_get_samples_count(void);

#ifdef __cplusplus
}
#endif

#endif