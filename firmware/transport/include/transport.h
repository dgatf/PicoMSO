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
 * PicoMSO transport abstraction layer (Phase 0).
 *
 * This header defines a minimal, generic, binary-oriented transport
 * interface.  It is intentionally transport-agnostic: no USB, CDC, bulk
 * endpoint, UART, PIO, ADC, or DMA details appear here.
 *
 * The protocol layer (firmware/protocol/) will call into this interface
 * to send and receive raw byte buffers.  Concrete backends (USB CDC,
 * USB bulk, UART, …) will be added in later phases by providing an
 * implementation that satisfies transport_interface_t.
 *
 * Constraints for Phase 0:
 *   - No dependency on TinyUSB, pico-sdk, or any hardware peripheral.
 *   - Callers supply opaque user-data pointers; the abstraction never
 *     inspects them.
 *   - Only the four helper functions listed below are exposed.
 */

#ifndef PICOMSO_TRANSPORT_H
#define PICOMSO_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Result codes
 * ----------------------------------------------------------------------- */

typedef enum {
    TRANSPORT_OK            = 0, /**< Operation completed successfully       */
    TRANSPORT_ERR_NULL      = 1, /**< NULL pointer passed for required arg   */
    TRANSPORT_ERR_NOT_READY = 2, /**< Transport not ready to send/receive    */
    TRANSPORT_ERR_IO        = 3, /**< Underlying send or receive failed      */
    TRANSPORT_ERR_PARTIAL   = 4, /**< Only part of the buffer was transferred */
} transport_result_t;

/* -----------------------------------------------------------------------
 * Interface: function-pointer table
 *
 * A concrete backend fills in this struct and passes it to
 * transport_init().  All function pointers must be non-NULL for the
 * transport to be considered ready.
 *
 * @param user_data  Opaque pointer forwarded from transport_ctx_t.
 *                   The backend may use it to hold its own state.
 * ----------------------------------------------------------------------- */

typedef struct transport_interface_t {
    /**
     * Return true if the transport is ready to transfer data.
     * May be NULL if the transport is always considered ready.
     */
    bool (*is_ready)(void *user_data);

    /**
     * Send exactly @p len bytes from @p buf.
     * Returns TRANSPORT_OK on success, TRANSPORT_ERR_IO on failure, or
     * TRANSPORT_ERR_PARTIAL if fewer than @p len bytes were sent.
     */
    transport_result_t (*send)(void *user_data, const uint8_t *buf, size_t len);

    /**
     * Receive up to @p len bytes into @p buf.
     * On return, @p *bytes_read is set to the number of bytes actually
     * placed into @p buf.  Returns TRANSPORT_OK on success or
     * TRANSPORT_ERR_IO on failure.
     */
    transport_result_t (*receive)(void *user_data, uint8_t *buf, size_t len,
                                  size_t *bytes_read);
} transport_interface_t;

/* -----------------------------------------------------------------------
 * Context: runtime state held by the caller
 * ----------------------------------------------------------------------- */

typedef struct transport_ctx_t {
    const transport_interface_t *iface;     /**< Pointer to backend vtable  */
    void                        *user_data; /**< Passed to every iface call */
} transport_ctx_t;

/* -----------------------------------------------------------------------
 * Helper API
 * ----------------------------------------------------------------------- */

/**
 * Initialise a transport context.
 *
 * Associates @p iface and @p user_data with @p ctx.  Does not call into
 * the backend; no hardware is touched.
 *
 * @param ctx       Context to initialise.  Must not be NULL.
 * @param iface     Backend interface.  Must not be NULL; send and receive
 *                  function pointers must be non-NULL.
 * @param user_data Opaque backend state; may be NULL.
 * @return TRANSPORT_OK on success, TRANSPORT_ERR_NULL if any required
 *         pointer is NULL.
 */
transport_result_t transport_init(transport_ctx_t             *ctx,
                                  const transport_interface_t *iface,
                                  void                        *user_data);

/**
 * Query whether the transport is ready to transfer data.
 *
 * If iface->is_ready is NULL the transport is treated as always ready.
 *
 * @param ctx  Initialised transport context.
 * @return true if ready, false otherwise.
 */
bool transport_is_ready(const transport_ctx_t *ctx);

/**
 * Send @p len bytes from @p buf via the transport.
 *
 * @param ctx  Initialised transport context.
 * @param buf  Bytes to send.  Must not be NULL.
 * @param len  Number of bytes to send.  If zero, returns TRANSPORT_OK
 *             immediately.
 * @return TRANSPORT_OK on success, or an error code on failure.
 */
transport_result_t transport_send(const transport_ctx_t *ctx,
                                  const uint8_t         *buf,
                                  size_t                 len);

/**
 * Receive up to @p len bytes into @p buf via the transport.
 *
 * @param ctx        Initialised transport context.
 * @param buf        Destination buffer.  Must not be NULL.
 * @param len        Maximum number of bytes to receive.
 * @param bytes_read Set to the number of bytes actually received.  Must
 *                   not be NULL.
 * @return TRANSPORT_OK on success, or an error code on failure.
 */
transport_result_t transport_receive(const transport_ctx_t *ctx,
                                     uint8_t               *buf,
                                     size_t                 len,
                                     size_t                *bytes_read);

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_TRANSPORT_H */
