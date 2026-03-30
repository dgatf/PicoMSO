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
 * PicoMSO integration layer – receive/dispatch/send loop (Phase 0).
 *
 * Wires together:
 *   transport_receive() → picomso_dispatch() → transport_send()
 *
 * No dependency on USB, UART, PIO, ADC, DMA, or any hardware peripheral.
 * No dependency on the logic-analyzer or oscilloscope firmware projects.
 */

#include "integration.h"

#include <string.h>

/* Maximum bytes that can arrive in a single packet: 8-byte header +
 * up to 512 bytes of payload (PICOMSO_MAX_PAYLOAD_LEN). */
#define INTEGRATION_RX_BUF_SIZE  (PICOMSO_PACKET_HEADER_SIZE + PICOMSO_MAX_PAYLOAD_LEN)

/* -----------------------------------------------------------------------
 * integration_init
 * ----------------------------------------------------------------------- */

integration_result_t integration_init(integration_ctx_t *ctx,
                                       transport_ctx_t   *transport)
{
    if (ctx == NULL || transport == NULL) {
        return INTEGRATION_ERR_NULL;
    }
    ctx->transport = transport;
    return INTEGRATION_OK;
}

/* -----------------------------------------------------------------------
 * integration_process_one
 * ----------------------------------------------------------------------- */

integration_result_t integration_process_one(integration_ctx_t *ctx)
{
    if (ctx == NULL || ctx->transport == NULL) {
        return INTEGRATION_ERR_NULL;
    }

    /* Step 1: receive one packet from the transport. */
    uint8_t  rx_buf[INTEGRATION_RX_BUF_SIZE];
    size_t   bytes_read = 0;

    transport_result_t tr = transport_receive(ctx->transport, rx_buf,
                                              sizeof(rx_buf), &bytes_read);
    if (tr != TRANSPORT_OK) {
        return INTEGRATION_ERR_RECV;
    }

    /* Nothing available yet – not an error. */
    if (bytes_read == 0) {
        return INTEGRATION_OK;
    }

    /* Step 2: pass the buffer to the protocol dispatch layer. */
    picomso_response_t resp;
    memset(&resp, 0, sizeof(resp));
    picomso_dispatch(rx_buf, bytes_read, &resp);

    /* A zero-used response indicates a catastrophic framing failure;
     * the protocol layer could not build any reply at all. */
    if (resp.used == 0) {
        return INTEGRATION_ERR_PROTO;
    }

    /* Step 3: send the response back through the transport. */
    tr = transport_send(ctx->transport, resp.buf, resp.used);
    if (tr != TRANSPORT_OK) {
        return INTEGRATION_ERR_SEND;
    }

    return INTEGRATION_OK;
}
