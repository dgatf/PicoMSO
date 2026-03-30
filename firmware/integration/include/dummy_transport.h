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
 * PicoMSO dummy/mock transport backend (Phase 0).
 *
 * Provides an in-memory implementation of transport_interface_t that is
 * used exclusively by the integration layer to exercise the full
 * receive → dispatch → send flow without any real hardware.
 *
 * Constraints:
 *   - No dependency on USB, UART, PIO, ADC, DMA, or any hardware peripheral.
 *   - No dependency on the Pico SDK or TinyUSB.
 *   - Suitable only for integration testing and architecture validation.
 */

#ifndef PICOMSO_DUMMY_TRANSPORT_H
#define PICOMSO_DUMMY_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "transport.h"

/* -----------------------------------------------------------------------
 * Buffer sizing
 *
 * Large enough to hold the maximum packet the protocol layer accepts
 * (8-byte header + 512-byte payload) plus the maximum response buffer
 * (256 bytes).
 * ----------------------------------------------------------------------- */

#define DUMMY_TRANSPORT_BUF_SIZE  1024u

/* -----------------------------------------------------------------------
 * State
 *
 * Callers allocate one dummy_transport_state_t and pass a pointer to it
 * as the user_data argument to transport_init().  The dummy receive()
 * function reads from rx_buf[rx_pos .. rx_len); the dummy send() function
 * appends into tx_buf[0 .. tx_len).
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t rx_buf[DUMMY_TRANSPORT_BUF_SIZE]; /**< Pre-loaded receive data     */
    size_t  rx_len;                           /**< Valid bytes in rx_buf        */
    size_t  rx_pos;                           /**< Read cursor inside rx_buf    */
    uint8_t tx_buf[DUMMY_TRANSPORT_BUF_SIZE]; /**< Bytes captured from send()   */
    size_t  tx_len;                           /**< Bytes written into tx_buf    */
} dummy_transport_state_t;

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

/**
 * Initialise state to empty (all buffers cleared, lengths zeroed).
 */
void dummy_transport_init(dummy_transport_state_t *state);

/**
 * Pre-load @p len bytes from @p buf so that the next receive() call
 * returns them.  Resets the read cursor to the start of rx_buf.
 *
 * @param state  Transport state to write into.  Must not be NULL.
 * @param buf    Source bytes.  Must not be NULL.
 * @param len    Number of bytes to copy; clamped to DUMMY_TRANSPORT_BUF_SIZE.
 */
void dummy_transport_set_rx(dummy_transport_state_t *state,
                             const uint8_t           *buf,
                             size_t                   len);

/**
 * Return the bytes that were written by the last send() call.
 *
 * @param state    Transport state to read from.  Must not be NULL.
 * @param len_out  Set to the number of valid bytes.  May be NULL.
 * @return Pointer to tx_buf (valid until the next dummy_transport_init or
 *         dummy_transport_set_rx call).
 */
const uint8_t *dummy_transport_get_tx(const dummy_transport_state_t *state,
                                       size_t                        *len_out);

/* -----------------------------------------------------------------------
 * Interface
 *
 * Pass this constant to transport_init() together with a pointer to a
 * dummy_transport_state_t as user_data.
 * ----------------------------------------------------------------------- */

extern const transport_interface_t dummy_transport_iface;

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_DUMMY_TRANSPORT_H */
