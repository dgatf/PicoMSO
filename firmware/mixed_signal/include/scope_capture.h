/*
 * PicoMSO - Mixed Signal Oscilloscope
 * Copyright (C) 2026 Daniel Gorbea
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#ifndef PICOMSO_SCOPE_CAPTURE_H
#define PICOMSO_SCOPE_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#if PICO_RP2040
#define SCOPE_RING_BITS 15u
#elif PICO_RP2350
#define SCOPE_RING_BITS 16u
#endif

#define SCOPE_BUFFER_SIZE (1u << SCOPE_RING_BITS)
#define SCOPE_CAPTURE_BLOCK_BYTES 64u
#define SCOPE_CAPTURE_MAX_SAMPLES SCOPE_BUFFER_SIZE
#define SCOPE_CAPTURE_ANALOG_CHANNEL_MAX 2u

void scope_capture_reset(void);
bool scope_capture_start(const capture_config_t *config, complete_handler_t handler);
bool scope_capture_prepare(const capture_config_t *config, complete_handler_t handler,
                           const capture_trigger_gate_t *trigger_gate);
bool scope_capture_arm(void);
void scope_capture_mark_capturing(void);
capture_state_t scope_capture_get_state(void);
bool scope_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len);
void scope_set_coupling(channel_t channel, coupling_t coupling);

#ifdef __cplusplus
}
#endif

#endif