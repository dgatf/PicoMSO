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

#ifndef PICOMSO_CAPTURE_CONTROLLER_H
#define PICOMSO_CAPTURE_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

// Which capture back-end is active.
// CAPTURE_MODE_UNSET is the power-on default (no back-end selected yet).
typedef enum capture_mode_t {
    CAPTURE_MODE_UNSET,
    CAPTURE_MODE_LOGIC,
    CAPTURE_MODE_OSCILLOSCOPE
} capture_mode_t;

// Minimal shared controller: tracks only mode and state.
// Hardware configuration, DMA, ADC, PIO, USB, triggers, and sample buffers
// remain entirely in the back-end firmware modules.
typedef struct capture_controller_t {
    capture_mode_t mode;
    capture_state_t state;
} capture_controller_t;

// Initialise the controller to CAPTURE_MODE_UNSET / CAPTURE_IDLE.
void capture_controller_init(capture_controller_t *ctrl);

// Set the active capture mode.
void capture_controller_set_mode(capture_controller_t *ctrl, capture_mode_t mode);

// Set the current capture state.
void capture_controller_set_state(capture_controller_t *ctrl, capture_state_t state);

// Return the current capture mode.
capture_mode_t capture_controller_get_mode(const capture_controller_t *ctrl);

// Return the current capture state.
capture_state_t capture_controller_get_state(const capture_controller_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif
