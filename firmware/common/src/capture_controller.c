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

#include "debug.h"

static const char *stream_name(uint8_t streams)
{
    switch (streams) {
    case PICOMSO_STREAM_NONE:
        return "none";
    case PICOMSO_STREAM_LOGIC:
        return "logic";
    case PICOMSO_STREAM_SCOPE:
        return "scope";
    case (PICOMSO_STREAM_LOGIC | PICOMSO_STREAM_SCOPE):
        return "logic|scope";
    default:
        return "invalid";
    }
}

static const char *capture_state_name(capture_state_t state)
{
    switch (state) {
    case CAPTURE_IDLE:
        return "IDLE";
    case CAPTURE_RUNNING:
        return "RUNNING";
    default:
        return "UNKNOWN";
    }
}

void capture_controller_init(capture_controller_t *ctrl)
{
    ctrl->streams_enabled = PICOMSO_STREAM_NONE;
    ctrl->state = CAPTURE_IDLE;
}

void capture_controller_set_streams(capture_controller_t *ctrl, uint8_t streams)
{
    debug("\n[capture_ctrl] set_streams prev=%s new=%s",
          stream_name(ctrl->streams_enabled), stream_name(streams));
    ctrl->streams_enabled = streams;
}

void capture_controller_set_state(capture_controller_t *ctrl, capture_state_t state)
{
    if (ctrl->state != state) {
        debug("\n[capture_ctrl] set_state prev=%s new=%s",
              capture_state_name(ctrl->state), capture_state_name(state));
    }
    ctrl->state = state;
}

uint8_t capture_controller_get_streams(const capture_controller_t *ctrl)
{
    return ctrl->streams_enabled;
}

capture_state_t capture_controller_get_state(const capture_controller_t *ctrl)
{
    return ctrl->state;
}
