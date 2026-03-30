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
 * PicoMSO USB transport backend (Phase 0).
 *
 * Adapts the custom RP2040 USB device driver (usb.c / usb.h) to the generic
 * transport_interface_t abstraction defined in firmware/transport/include/transport.h.
 *
 * This is a control-plane-only transport:
 *   - Host → device commands arrive as vendor OUT control transfers (EP0_OUT).
 *   - Device → host responses are sent as bulk IN transfers (EP6_IN).
 *   - Capture data streaming is out of scope for this backend.
 *
 * Usage:
 *   1. Call usb_transport_init() once at startup (initialises the USB hardware).
 *   2. Call transport_init(&ctx, &usb_transport_iface, NULL) to bind the interface.
 *   3. Pass &ctx to integration_init() as usual.
 *
 * The protocol layer remains fully transport-agnostic; it never includes or
 * depends on this header or any USB-specific symbol.
 */

#ifndef PICOMSO_USB_TRANSPORT_H
#define PICOMSO_USB_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "transport.h"

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/**
 * Initialise the USB hardware.
 *
 * Thin wrapper around usb_device_init().  Must be called once before the
 * first transport_send / transport_receive call.
 */
void usb_transport_init(void);

/**
 * USB transport interface.
 *
 * Pass to transport_init() with user_data = NULL:
 *
 *   transport_ctx_t ctx;
 *   transport_init(&ctx, &usb_transport_iface, NULL);
 *
 * The interface maps:
 *   is_ready  → usb_is_configured()
 *   send      → EP6_IN bulk transfer (device → host)
 *   receive   → EP0_OUT control transfer (host → device), polled
 */
extern const transport_interface_t usb_transport_iface;

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_USB_TRANSPORT_H */
