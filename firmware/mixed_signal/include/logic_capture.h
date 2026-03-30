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

#define LOGIC_CAPTURE_BLOCK_BYTES          64u
#define LOGIC_CAPTURE_TOTAL_SAMPLES        32u
#define LOGIC_CAPTURE_PRE_TRIGGER_SAMPLES  16u

void logic_capture_reset(void);
void logic_capture_arm(void);
capture_state_t logic_capture_get_state(void);
bool logic_capture_read_block(uint8_t *block_id, uint8_t *data, uint16_t *data_len);

#ifdef __cplusplus
}
#endif

#endif
