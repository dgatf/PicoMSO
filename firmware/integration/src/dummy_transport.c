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
 * PicoMSO dummy/mock transport backend – implementation (Phase 0).
 *
 * Provides an in-memory send/receive pair backed by fixed-size byte arrays.
 * All operations are synchronous and never block.  Suitable for exercising
 * the integration layer without any hardware or OS dependency.
 */

#include "dummy_transport.h"

#include <string.h>

/* -----------------------------------------------------------------------
 * Backend callbacks
 * ----------------------------------------------------------------------- */

static bool dummy_is_ready(void *user_data)
{
    (void)user_data;
    return true;
}

static transport_result_t dummy_send(void *user_data, const uint8_t *buf,
                                     size_t len)
{
    dummy_transport_state_t *state = (dummy_transport_state_t *)user_data;

    if (len > DUMMY_TRANSPORT_BUF_SIZE) {
        len = DUMMY_TRANSPORT_BUF_SIZE;
    }
    memcpy(state->tx_buf, buf, len);
    state->tx_len = len;
    return TRANSPORT_OK;
}

static transport_result_t dummy_receive(void *user_data, uint8_t *buf,
                                        size_t len, size_t *bytes_read)
{
    dummy_transport_state_t *state = (dummy_transport_state_t *)user_data;

    size_t available = state->rx_len - state->rx_pos;
    size_t to_copy   = (available < len) ? available : len;

    memcpy(buf, state->rx_buf + state->rx_pos, to_copy);
    state->rx_pos += to_copy;
    *bytes_read = to_copy;
    return TRANSPORT_OK;
}

/* -----------------------------------------------------------------------
 * Public interface constant
 * ----------------------------------------------------------------------- */

const transport_interface_t dummy_transport_iface = {
    .is_ready = dummy_is_ready,
    .send     = dummy_send,
    .receive  = dummy_receive,
};

/* -----------------------------------------------------------------------
 * Helper implementations
 * ----------------------------------------------------------------------- */

void dummy_transport_init(dummy_transport_state_t *state)
{
    memset(state, 0, sizeof(*state));
}

void dummy_transport_set_rx(dummy_transport_state_t *state,
                             const uint8_t           *buf,
                             size_t                   len)
{
    if (len > DUMMY_TRANSPORT_BUF_SIZE) {
        len = DUMMY_TRANSPORT_BUF_SIZE;
    }
    memcpy(state->rx_buf, buf, len);
    state->rx_len = len;
    state->rx_pos = 0;
}

const uint8_t *dummy_transport_get_tx(const dummy_transport_state_t *state,
                                       size_t                        *len_out)
{
    if (len_out != NULL) {
        *len_out = state->tx_len;
    }
    return state->tx_buf;
}
