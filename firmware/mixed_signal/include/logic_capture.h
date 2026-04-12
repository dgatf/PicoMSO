/*
 * PicoMSO - Mixed Signal Oscilloscope
 * Copyright (C) 2026 Daniel Gorbea
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#ifndef PICOMSO_LOGIC_CAPTURE_H
#define PICOMSO_LOGIC_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

#if PICO_RP2040
#define LOGIC_RING_BITS 15u
#elif PICO_RP2350
#define LOGIC_RING_BITS 16u
#endif

#define LOGIC_BUFFER_SIZE (1u << LOGIC_RING_BITS)
#define LOGIC_CAPTURE_BLOCK_BYTES 64u
#define LOGIC_CAPTURE_MAX_SAMPLES LOGIC_BUFFER_SIZE

typedef struct logic_capture_activation_t {
    // Precomputed direct-write masks for the final logic activation point.
    // Mixed mode uses these to write pio0->ctrl / pio1->ctrl in the intended
    // order once all DMA and PIO state machines are already armed.
    uint32_t pio0_enable_mask;
    uint32_t pio1_enable_mask;
} logic_capture_activation_t;

void logic_capture_reset(void);
bool logic_capture_prepare(const capture_config_t *config, complete_handler_t handler,
                           capture_trigger_gate_t *trigger_gate, logic_capture_activation_t *activation);
bool logic_capture_arm(void);
void logic_capture_activate(const logic_capture_activation_t *activation);
void logic_capture_mark_capturing(void);
bool logic_capture_start(const capture_config_t *config, complete_handler_t handler);
capture_state_t logic_capture_get_state(void);
bool logic_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len);

#ifdef __cplusplus
}
#endif

#endif
