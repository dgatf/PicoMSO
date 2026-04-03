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
 * PicoMSO integration layer (Phase 0).
 *
 * This header exposes the minimal API that wires together:
 *   - the transport receive path  (firmware/transport/)
 *   - the protocol dispatch layer (firmware/protocol/)
 *   - the transport send path     (firmware/transport/)
 *
 * The integration layer is intentionally thin and transport-agnostic.
 * It owns no protocol or transport state itself; callers supply an
 * initialised transport_ctx_t backed by any concrete implementation:
 *   - dummy_transport_iface  (in-memory mock, no hardware dependency)
 *   - usb_transport_iface    (real RP2040 USB hardware backend)
 *
 * Control-plane scope (this phase):
 *   - GET_INFO, GET_CAPABILITIES, GET_STATUS, SET_MODE
 *   - No capture data streaming, no ADC/PIO/DMA operations
 *   - No dependency on the logic-analyzer or oscilloscope firmware
 */

#ifndef PICOMSO_INTEGRATION_H
#define PICOMSO_INTEGRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "protocol.h"
#include "transport.h"

/* -----------------------------------------------------------------------
 * Result codes
 * ----------------------------------------------------------------------- */

typedef enum {
    INTEGRATION_OK = 0,        /**< One packet received, dispatched, sent  */
    INTEGRATION_ERR_NULL = 1,  /**< Required pointer argument was NULL     */
    INTEGRATION_ERR_RECV = 2,  /**< Transport receive failed               */
    INTEGRATION_ERR_SEND = 3,  /**< Transport send failed                  */
    INTEGRATION_ERR_PROTO = 4, /**< Protocol layer produced no response    */
} integration_result_t;

/* -----------------------------------------------------------------------
 * Context
 *
 * Holds a pointer to the caller-owned transport context.  The integration
 * layer does not own the transport; the caller is responsible for the
 * lifetime of *transport.
 * ----------------------------------------------------------------------- */

typedef struct {
    transport_ctx_t *transport; /**< Initialised transport context (not owned) */
} integration_ctx_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/**
 * Initialise an integration context.
 *
 * Associates @p transport with @p ctx.  No I/O is performed.
 *
 * @param ctx        Context to initialise.  Must not be NULL.
 * @param transport  Initialised transport context.  Must not be NULL.
 * @return INTEGRATION_OK on success, INTEGRATION_ERR_NULL if any pointer
 *         is NULL.
 */
integration_result_t integration_init(integration_ctx_t *ctx, transport_ctx_t *transport);

/**
 * Process one packet: receive → dispatch → send.
 *
 * 1. Calls transport_receive() to read up to one full packet from the
 *    transport into an internal stack buffer.
 * 2. If bytes were received, calls picomso_dispatch() to parse the
 *    packet and build a response.
 * 3. Calls transport_send() to return the response bytes to the transport.
 *
 * If the transport returns zero bytes (nothing available yet), the call
 * returns INTEGRATION_OK immediately with no side-effects.
 *
 * @param ctx  Initialised integration context.  Must not be NULL.
 * @return INTEGRATION_OK on success or when no data was available.
 *         An error code if receive, protocol dispatch, or send failed.
 */
integration_result_t integration_process_one(integration_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_INTEGRATION_H */
