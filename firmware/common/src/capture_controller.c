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

#include "capture_controller.h"

void capture_controller_init(capture_controller_t *ctrl) {
    ctrl->mode = CAPTURE_MODE_UNSET;
    ctrl->state = CAPTURE_IDLE;
}

void capture_controller_set_mode(capture_controller_t *ctrl, capture_mode_t mode) {
    ctrl->mode = mode;
}

void capture_controller_set_state(capture_controller_t *ctrl, capture_state_t state) {
    ctrl->state = state;
}

capture_mode_t capture_controller_get_mode(const capture_controller_t *ctrl) {
    return ctrl->mode;
}

capture_state_t capture_controller_get_state(const capture_controller_t *ctrl) {
    return ctrl->state;
}
