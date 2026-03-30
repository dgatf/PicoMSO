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
 * PicoMSO USB CDC transport backend – implementation.
 *
 * All TinyUSB and Pico SDK includes are isolated here.  Nothing outside
 * this translation unit needs to include tusb.h or any SDK header in
 * order to use the transport interface.
 *
 * Control-plane focus:
 *   - is_ready  : returns true when a CDC host is connected.
 *   - send      : writes bytes to the CDC TX FIFO and flushes.
 *   - receive   : reads available bytes from the CDC RX FIFO.
 *
 * The caller is responsible for calling tud_task() regularly from the
 * main loop so that the TinyUSB stack can process USB events.
 */

#include "usb_transport.h"

/* TinyUSB device stack – strictly localised to this translation unit. */
#include "tusb.h"

/* -----------------------------------------------------------------------
 * Backend callbacks
 * ----------------------------------------------------------------------- */

static bool usb_is_ready(void *user_data)
{
    const usb_transport_state_t *state = (const usb_transport_state_t *)user_data;
    return tud_cdc_n_connected(state->itf);
}

static transport_result_t usb_send(void *user_data, const uint8_t *buf,
                                   size_t len)
{
    const usb_transport_state_t *state = (const usb_transport_state_t *)user_data;

    uint32_t written = tud_cdc_n_write(state->itf, buf, (uint32_t)len);
    tud_cdc_n_write_flush(state->itf);

    if (written == 0 && len > 0) {
        return TRANSPORT_ERR_IO;
    }
    if ((size_t)written < len) {
        return TRANSPORT_ERR_PARTIAL;
    }
    return TRANSPORT_OK;
}

static transport_result_t usb_receive(void *user_data, uint8_t *buf,
                                      size_t len, size_t *bytes_read)
{
    const usb_transport_state_t *state = (const usb_transport_state_t *)user_data;

    uint32_t available = tud_cdc_n_available(state->itf);
    if (available == 0) {
        *bytes_read = 0;
        return TRANSPORT_OK;
    }

    uint32_t to_read = (available < (uint32_t)len) ? available : (uint32_t)len;
    uint32_t count   = tud_cdc_n_read(state->itf, buf, to_read);
    *bytes_read = (size_t)count;
    return TRANSPORT_OK;
}

/* -----------------------------------------------------------------------
 * Public interface constant
 * ----------------------------------------------------------------------- */

const transport_interface_t usb_transport_iface = {
    .is_ready = usb_is_ready,
    .send     = usb_send,
    .receive  = usb_receive,
};

/* -----------------------------------------------------------------------
 * Initialisation helper
 * ----------------------------------------------------------------------- */

void usb_transport_init(usb_transport_state_t *state)
{
    state->itf = 0;
}
