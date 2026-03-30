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
 * Minimal capture buffer provider for PicoMSO.
 *
 * Provides a small, hardware-free digital sample buffer that the protocol
 * layer can use to satisfy READ_DATA_BLOCK requests without embedding a
 * dummy payload directly in protocol_dispatch.c.
 *
 * The buffer is intentionally minimal:
 *   - Digital-only sample data (no analog / mixed-signal).
 *   - Fixed-size block of bytes generated at initialisation time.
 *   - No ADC, PIO, DMA, or any other hardware dependency.
 *   - No streaming; each call to capture_buffer_provider_get_block()
 *     returns the same sample content with an incrementing block_id.
 *
 * This is a transitional layer.  In a future phase it will be replaced (or
 * extended) by a real hardware-backed capture path.
 */

#ifndef PICOMSO_CAPTURE_BUFFER_H
#define PICOMSO_CAPTURE_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */

/** Number of digital sample bytes in one capture block. */
#define CAPTURE_BUFFER_BLOCK_SIZE  64u

/* -----------------------------------------------------------------------
 * capture_buffer_t
 *
 * Minimal digital sample buffer.  Holds one fixed block of sample bytes
 * and a monotonically incrementing block sequence counter.
 *
 * No hardware state is stored here.  The sample content is generated once
 * at initialisation time (capture_buffer_init) and does not change between
 * calls.  The block_id is the only field that advances with each request.
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t samples[CAPTURE_BUFFER_BLOCK_SIZE]; /**< Digital sample bytes   */
    uint8_t block_id;                           /**< Next block sequence number */
} capture_buffer_t;

/* -----------------------------------------------------------------------
 * API
 * ----------------------------------------------------------------------- */

/**
 * Initialise the capture buffer.
 *
 * Fills buf->samples with a 0x00…(CAPTURE_BUFFER_BLOCK_SIZE-1) ramp pattern
 * representing minimal digital sample data.  Sets buf->block_id to 0.
 *
 * @param buf  Pointer to the capture_buffer_t to initialise.  Must not be NULL.
 */
void capture_buffer_init(capture_buffer_t *buf);

/**
 * Retrieve the next sample block from the capture buffer provider.
 *
 * Copies the current sample data into @p data_out, writes the current
 * block_id into @p block_id_out, writes the number of bytes copied into
 * @p data_len_out, and then advances buf->block_id for the next call.
 *
 * @p data_out must point to at least CAPTURE_BUFFER_BLOCK_SIZE bytes.
 *
 * @param buf          Capture buffer state.  Must not be NULL.
 * @param block_id_out Receives the block sequence number.  Must not be NULL.
 * @param data_out     Destination for sample bytes.  Must not be NULL.
 * @param data_len_out Receives the number of bytes written.  Must not be NULL.
 */
void capture_buffer_provider_get_block(capture_buffer_t *buf,
                                       uint8_t          *block_id_out,
                                       uint8_t          *data_out,
                                       uint16_t         *data_len_out);

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_CAPTURE_BUFFER_H */
