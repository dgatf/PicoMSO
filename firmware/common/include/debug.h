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

#ifndef PICOMSO_DEBUG_H
#define PICOMSO_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "pico/types.h"

#define DEBUG_BUFFER_SIZE 300

// UART0 is used for debug output on both firmware projects
#define DEBUG_UART_TX_GPIO 16
#define DEBUG_UART_RX_GPIO 17

// Pull this GPIO low at boot to enable debug output (active-low, pull-up default)
#define DEBUG_ENABLE_GPIO 18

void debug_init(uint baudrate, char *buffer, bool *is_enabled);
void debug_reinit(void);
void debug(const char *format, ...);
void debug_block(const char *format, ...);
bool debug_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
