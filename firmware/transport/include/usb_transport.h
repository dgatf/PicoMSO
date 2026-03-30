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

/*
 * PicoMSO USB CDC transport backend.
 *
 * Provides a concrete implementation of transport_interface_t that sends
 * and receives raw bytes over a USB CDC ACM (virtual serial) connection
 * using TinyUSB.
 *
 * Design constraints:
 *   - This header is free of TinyUSB and Pico SDK includes.  All SDK
 *     details are confined to usb_transport.c.
 *   - The backend is focused on the control plane only: it transfers
 *     small command/response packets and does not stream capture data.
 *   - No ADC, PIO, DMA, or capture-hardware dependency.
 *   - Callers must drive the TinyUSB task loop (tud_task()) from their
 *     main loop; the transport does not call tud_task() itself.
 *
 * Usage:
 *   1. Declare a usb_transport_state_t and call usb_transport_init().
 *   2. Call transport_init() with &usb_transport_iface and a pointer to
 *      your usb_transport_state_t as user_data.
 *   3. Use transport_send() / transport_receive() as normal.
 *   4. Ensure tud_task() is called regularly from the main loop.
 */

#ifndef PICOMSO_USB_TRANSPORT_H
#define PICOMSO_USB_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "transport.h"

/* -----------------------------------------------------------------------
 * State
 *
 * Callers allocate one usb_transport_state_t and pass a pointer to it as
 * the user_data argument to transport_init().
 *
 * Fields:
 *   itf  – TinyUSB CDC interface index.  Use 0 for the first (and
 *           typically only) CDC interface.
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t itf; /**< TinyUSB CDC interface index (0-based) */
} usb_transport_state_t;

/* -----------------------------------------------------------------------
 * Initialisation helper
 * ----------------------------------------------------------------------- */

/**
 * Initialise a usb_transport_state_t.
 *
 * Sets the CDC interface index to 0 (the default single-CDC configuration).
 * Call this before passing the state to transport_init().
 *
 * @param state  Must not be NULL.
 */
void usb_transport_init(usb_transport_state_t *state);

/* -----------------------------------------------------------------------
 * Interface
 *
 * Pass this constant to transport_init() together with a pointer to a
 * usb_transport_state_t as user_data.
 *
 * This target is only available when the picomso_usb_transport CMake
 * library target has been linked (requires tinyusb_device).
 * ----------------------------------------------------------------------- */

extern const transport_interface_t usb_transport_iface;

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_USB_TRANSPORT_H */
