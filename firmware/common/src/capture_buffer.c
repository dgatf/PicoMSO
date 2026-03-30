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

#include "capture_buffer.h"

#include <string.h>

void capture_buffer_init(capture_buffer_t *buf)
{
    /* Ramp pattern: 0x00, 0x01, …, (CAPTURE_BUFFER_BLOCK_SIZE-1).
     * Represents minimal digital sample data for end-to-end path testing.
     * No hardware is involved; this is a software-generated placeholder. */
    for (uint8_t i = 0u; i < CAPTURE_BUFFER_BLOCK_SIZE; i++) {
        buf->samples[i] = i;
    }
    buf->block_id = 0u;
}

void capture_buffer_provider_get_block(capture_buffer_t *buf,
                                       uint8_t          *block_id_out,
                                       uint8_t          *data_out,
                                       uint16_t         *data_len_out)
{
    *block_id_out = buf->block_id++;
    memcpy(data_out, buf->samples, CAPTURE_BUFFER_BLOCK_SIZE);
    *data_len_out = (uint16_t)CAPTURE_BUFFER_BLOCK_SIZE;
}
