/*
 * PicoMSO - Mixed Signal Oscilloscope
 * Copyright (C) 2026 Daniel Gorbea
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "logic_capture.h"

#include <string.h>

#include "debug.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "logic_capture.pio.h"
#include "pico/stdlib.h"

#define LOGIC_CAPTURE_CHANNEL_COUNT 16u
#define LOGIC_CAPTURE_MAX_TRIGGER_COUNT 2u
#define LOGIC_CAPTURE_RATE_CHANGE_CLK_HZ 500000u

typedef enum logic_capture_phase_t {
    LOGIC_CAPTURE_PHASE_DISARMED = 0,
    LOGIC_CAPTURE_PHASE_ARMED,
    LOGIC_CAPTURE_PHASE_CAPTURING,
    LOGIC_CAPTURE_PHASE_ABORTING,
    LOGIC_CAPTURE_PHASE_FINALIZED
} logic_capture_phase_t;

static capture_config_t s_logic_capture_config = {
    .total_samples = 0u,
    .rate = 0u,
    .pre_trigger_samples = 0u,
    .channels = LOGIC_CAPTURE_CHANNEL_COUNT,
    .trigger =
        {
            {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
            {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
            {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
            {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
        },
};

static volatile logic_capture_phase_t s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
static uint32_t s_capture_read_offset_bytes = 0u;
static bool s_activation_armed = false;

static const uint s_sm_capture = 0u;
static const uint s_sm_counter = 1u;
static const uint s_sm_mux = 2u;
static const uint s_sm_trigger[LOGIC_CAPTURE_MAX_TRIGGER_COUNT] = {0u, 1u};

static const uint s_dma_capture = 0u;
static const uint s_dma_capture_reload = 1u;
static const uint s_dma_set_counter = 2u;
static const uint s_dma_halt_capture = 3u;
static const uint s_dma_trigger_to_mux[LOGIC_CAPTURE_MAX_TRIGGER_COUNT] = {4u, 5u};

static const uint s_triggered_channel_index[LOGIC_CAPTURE_MAX_TRIGGER_COUNT] = {0u, 1u};
static uint s_reload_counter;

static uint s_offset_capture;
static uint s_offset_counter;
static uint s_offset_mux;
static uint s_offset_trigger[LOGIC_CAPTURE_MAX_TRIGGER_COUNT];

static uint s_pre_trigger_samples;
static uint s_post_trigger_samples;
static uint s_rate;

static uint s_trigger_count;
static uint s_sm_trigger_mask;

static volatile int s_first_sample;
static volatile int s_triggered_channel;

static float s_clk_div;
static uint s_pio_ctrl_halt = 0;

static uint16_t s_sample_buffer[LOGIC_BUFFER_SIZE] __attribute__((aligned(LOGIC_BUFFER_SIZE * sizeof(uint16_t))));

static pio_sm_config s_pio_config_capture;
static pio_sm_config s_pio_config_counter;
static pio_sm_config s_pio_config_mux;
static pio_sm_config s_pio_config_trigger[LOGIC_CAPTURE_MAX_TRIGGER_COUNT];

static void (*s_complete_handler)(void) = NULL;

static const char *logic_capture_phase_name(logic_capture_phase_t phase);
static inline bool logic_capture_configure_trigger(trigger_t trigger);
static inline void logic_capture_trigger_handler(void);
static inline void logic_capture_complete_handler(void);
static inline void logic_capture_stop_hardware(void);
static void logic_capture_configure_inputs(void);
static uint16_t logic_capture_get_sample_index(int index);

static const char *logic_capture_phase_name(logic_capture_phase_t phase) {
    switch (phase) {
        case LOGIC_CAPTURE_PHASE_DISARMED:
            return "DISARMED";
        case LOGIC_CAPTURE_PHASE_ARMED:
            return "ARMED";
        case LOGIC_CAPTURE_PHASE_CAPTURING:
            return "CAPTURING";
        case LOGIC_CAPTURE_PHASE_ABORTING:
            return "ABORTING";
        case LOGIC_CAPTURE_PHASE_FINALIZED:
            return "FINALIZED";
        default:
            return "UNKNOWN";
    }
}

static inline bool logic_capture_configure_trigger(trigger_t trigger) {
    if (s_trigger_count >= LOGIC_CAPTURE_MAX_TRIGGER_COUNT) {
        return false;
    }

    switch (trigger.match) {
        case TRIGGER_TYPE_LEVEL_HIGH:
            s_offset_trigger[s_trigger_count] = pio_add_program(pio1, &trigger_level_high_program);
            s_pio_config_trigger[s_trigger_count] =
                trigger_level_high_program_get_default_config(s_offset_trigger[s_trigger_count]);
            break;
        case TRIGGER_TYPE_LEVEL_LOW:
            s_offset_trigger[s_trigger_count] = pio_add_program(pio1, &trigger_level_low_program);
            s_pio_config_trigger[s_trigger_count] =
                trigger_level_low_program_get_default_config(s_offset_trigger[s_trigger_count]);
            break;
        case TRIGGER_TYPE_EDGE_HIGH:
            s_offset_trigger[s_trigger_count] = pio_add_program(pio1, &trigger_edge_high_program);
            s_pio_config_trigger[s_trigger_count] =
                trigger_edge_high_program_get_default_config(s_offset_trigger[s_trigger_count]);
            break;
        case TRIGGER_TYPE_EDGE_LOW:
            s_offset_trigger[s_trigger_count] = pio_add_program(pio1, &trigger_edge_low_program);
            s_pio_config_trigger[s_trigger_count] =
                trigger_edge_low_program_get_default_config(s_offset_trigger[s_trigger_count]);
            break;
    }

    sm_config_set_clkdiv(&s_pio_config_trigger[s_trigger_count], s_clk_div);
    sm_config_set_in_pins(&s_pio_config_trigger[s_trigger_count], trigger.pin);
    pio_sm_init(pio1, s_sm_trigger[s_trigger_count], s_offset_trigger[s_trigger_count],
                &s_pio_config_trigger[s_trigger_count]);

    s_sm_trigger_mask |= 1u << s_sm_trigger[s_trigger_count];

    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_trigger_to_mux[s_trigger_count]);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
        channel_config_set_write_increment(&dma_cfg, false);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_dreq(&dma_cfg, pio_get_dreq(pio1, s_sm_trigger[s_trigger_count], false));

        dma_channel_configure(s_dma_trigger_to_mux[s_trigger_count], &dma_cfg, &pio0->txf[s_sm_mux],
                              &s_triggered_channel_index[s_trigger_count], 1u, false);
    }

    if (debug_is_enabled()) {
        char match[15] = "";

        switch (s_logic_capture_config.trigger[s_trigger_count].match) {
            case TRIGGER_TYPE_LEVEL_HIGH:
                strcpy(match, "Level High");
                break;
            case TRIGGER_TYPE_LEVEL_LOW:
                strcpy(match, "Level Low");
                break;
            case TRIGGER_TYPE_EDGE_HIGH:
                strcpy(match, "Edge High");
                break;
            case TRIGGER_TYPE_EDGE_LOW:
                strcpy(match, "Edge Low");
                break;
        }

        debug_block("\n[logic] trigger configured index=%u pin=%u match=%s", s_trigger_count,
                    s_logic_capture_config.trigger[s_trigger_count].pin, match);
    }

    ++s_trigger_count;
    return true;
}

static inline void logic_capture_trigger_handler(void) {
    pio_interrupt_clear(pio0, 0u);
    s_triggered_channel = pio_sm_get(pio0, s_sm_mux);
    debug("\n[logic] trigger fired channel=%d", s_triggered_channel);
}

static inline void logic_capture_complete_handler(void) {
    pio_interrupt_clear(pio0, 1u);

    debug("\n[logic] complete irq entered phase=%s transfer_count=%lu", logic_capture_phase_name(s_phase),
          (unsigned long)dma_hw->ch[s_dma_capture].transfer_count);
    if (s_phase != LOGIC_CAPTURE_PHASE_CAPTURING) {
        debug("\n[logic] complete ignored early phase=%s", logic_capture_phase_name(s_phase));
        return;
    }

    int pos = LOGIC_BUFFER_SIZE - dma_hw->ch[s_dma_capture].transfer_count;
    s_first_sample = pos - s_pre_trigger_samples - s_post_trigger_samples;
    if (s_first_sample < 0) {
        s_first_sample += LOGIC_BUFFER_SIZE;
    }

    logic_capture_stop_hardware();
    s_phase = LOGIC_CAPTURE_PHASE_FINALIZED;

    debug("\n[logic] complete phase=%s triggered_channel=%d", logic_capture_phase_name(s_phase), s_triggered_channel);

    if (s_complete_handler != NULL) {
        s_complete_handler();
    }
}

static inline void logic_capture_stop_hardware(void) {
    pio_set_sm_mask_enabled(pio0, (1u << s_sm_capture) | (1u << s_sm_mux) | (1u << s_sm_counter), false);
    pio_set_sm_mask_enabled(pio1, s_sm_trigger_mask, false);
    pio_set_irq0_source_enabled(pio0, (enum pio_interrupt_source)pis_interrupt0, false);
    pio_set_irq1_source_enabled(pio0, (enum pio_interrupt_source)pis_interrupt1, false);

    dma_channel_abort(s_dma_capture);
    dma_channel_abort(s_dma_capture_reload);
    dma_channel_abort(s_dma_set_counter);
    dma_channel_abort(s_dma_halt_capture);

    for (uint i = 0u; i < s_trigger_count; ++i) {
        dma_channel_abort(s_dma_trigger_to_mux[i]);
        pio_sm_clear_fifos(pio1, s_sm_trigger[i]);
    }

    if (s_trigger_count) pio_sm_clear_fifos(pio0, s_sm_mux);
    pio_sm_clear_fifos(pio0, s_sm_capture);
    pio_sm_clear_fifos(pio0, s_sm_counter);
    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);
}

static void logic_capture_configure_inputs(void) {
    for (uint32_t pin = 0u; pin < LOGIC_CAPTURE_CHANNEL_COUNT; ++pin) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_pull_down(pin);
    }
}

static uint16_t logic_capture_get_sample_index(int index) {
    const uint total_samples = s_pre_trigger_samples + s_post_trigger_samples;

    if (index < 0 || (uint)index >= total_samples) {
        return 0u;
    }

    int pos = s_first_sample + index;

    if (pos < 0u) {
        pos += LOGIC_BUFFER_SIZE;
    } else if (pos >= (int)LOGIC_BUFFER_SIZE) {
        pos -= LOGIC_BUFFER_SIZE;
    }

    return s_sample_buffer[pos];
}

void logic_capture_reset(void) {
    debug("\n[logic] reset begin phase=%s total_samples=%lu read_offset=%lu", logic_capture_phase_name(s_phase),
          (unsigned long)s_logic_capture_config.total_samples, (unsigned long)s_capture_read_offset_bytes);

    if (clock_get_hz(clk_sys) != 100000000u) {
        set_sys_clock_khz(100000u, true);
        debug_reinit();
    }

    if (s_phase == LOGIC_CAPTURE_PHASE_CAPTURING) {
        s_phase = LOGIC_CAPTURE_PHASE_ABORTING;
        logic_capture_stop_hardware();
    }

    s_logic_capture_config.total_samples = 0u;
    s_logic_capture_config.rate = 0u;
    s_logic_capture_config.pre_trigger_samples = 0u;

    s_capture_read_offset_bytes = 0u;
    s_pre_trigger_samples = 0u;
    s_post_trigger_samples = 0u;
    s_first_sample = 0;
    s_trigger_count = 0u;
    s_sm_trigger_mask = 0u;
    s_triggered_channel = -1;
    s_activation_armed = false;
    s_phase = LOGIC_CAPTURE_PHASE_DISARMED;

    debug("\n[logic] reset done phase=%s", logic_capture_phase_name(s_phase));
}

bool logic_capture_prepare(const capture_config_t *config, complete_handler_t handler,
                           capture_trigger_gate_t *trigger_gate, logic_capture_activation_t *activation) {
    if (config == NULL || handler == NULL || trigger_gate == NULL || activation == NULL) {
        debug("\n[logic] prepare rejected reason=bad_args config=%u handler=%u gate=%u activation=%u", config != NULL,
              handler != NULL, trigger_gate != NULL, activation != NULL);
        return false;
    }

    if (s_phase != LOGIC_CAPTURE_PHASE_DISARMED && s_phase != LOGIC_CAPTURE_PHASE_FINALIZED) {
        debug("\n[logic] prepare rejected reason=busy phase=%s", logic_capture_phase_name(s_phase));
        return false;
    }

    if (config->total_samples == 0u || config->total_samples > LOGIC_CAPTURE_MAX_SAMPLES) {
        debug("\n[logic] prepare rejected reason=invalid_total_samples samples=%lu max=%u",
              (unsigned long)config->total_samples, LOGIC_CAPTURE_MAX_SAMPLES);
        return false;
    }

    if (config->pre_trigger_samples > config->total_samples) {
        debug("\n[logic] prepare rejected reason=pre_gt_total pre=%lu samples=%lu",
              (unsigned long)config->pre_trigger_samples, (unsigned long)config->total_samples);
        return false;
    }

    s_complete_handler = handler;
    s_logic_capture_config = *config;
    s_logic_capture_config.channels = LOGIC_CAPTURE_CHANNEL_COUNT;
    s_capture_read_offset_bytes = 0u;

    {
        const uint32_t samples = config->total_samples;
        const uint32_t rate = config->rate;
        const uint32_t pre_trigger_samples = config->pre_trigger_samples;

        debug("\n[logic] prepare request phase=%s samples=%lu rate=%lu pre=%lu", logic_capture_phase_name(s_phase),
              (unsigned long)samples, (unsigned long)rate, (unsigned long)pre_trigger_samples);

        if (samples == 0u) {
            debug("\n[logic] prepare rejected reason=zero_samples");
            return false;
        }

        if (pre_trigger_samples > samples) {
            debug("\n[logic] prepare rejected reason=pre_gt_total pre=%lu samples=%lu",
                  (unsigned long)pre_trigger_samples, (unsigned long)samples);
            return false;
        }

        if (samples > LOGIC_BUFFER_SIZE) {
            debug("\n[logic] prepare rejected reason=buffer_overflow samples=%lu max=%u", (unsigned long)samples,
                  LOGIC_BUFFER_SIZE);
            return false;
        }

        if (rate == 0u) {
            debug("\n[logic] prepare rejected reason=zero_rate");
            return false;
        }
    }

    logic_capture_configure_inputs();

    s_pre_trigger_samples = config->pre_trigger_samples;
    s_post_trigger_samples = config->total_samples - config->pre_trigger_samples;
    s_rate = config->rate;
    s_first_sample = 0;
    s_trigger_count = 0u;
    s_sm_trigger_mask = 0u;
    s_triggered_channel = -1;
    s_activation_armed = false;

    if (s_rate > LOGIC_CAPTURE_RATE_CHANGE_CLK_HZ) {
        if (clock_get_hz(clk_sys) != 200000000u) {
            set_sys_clock_khz(200000u, true);
            debug_reinit();
        }
    } else if (clock_get_hz(clk_sys) != 100000000u) {
        set_sys_clock_khz(100000u, true);
        debug_reinit();
    }

    s_clk_div = (float)clock_get_hz(clk_sys) / (float)s_rate;
    if (s_clk_div > 65535.0f) {
        s_clk_div = 65535.0f;
    }

    debug_block("\n[logic] prepare clocks sys_clk=%u clk_div=%f", clock_get_hz(clk_sys), s_clk_div);

    // Set triggers
    for (uint i = 0u; i < LOGIC_CAPTURE_MAX_TRIGGER_COUNT; ++i) {
        if (s_logic_capture_config.trigger[i].is_enabled &&
            !logic_capture_configure_trigger(s_logic_capture_config.trigger[i])) {
            debug_block("\n[logic] prepare rejected reason=trigger_setup_failed index=%u", i);
            logic_capture_stop_hardware();
            s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
            return false;
        }
    }

    s_reload_counter = LOGIC_BUFFER_SIZE;
    
    // DMA capture
    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_capture);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
        channel_config_set_ring(&dma_cfg, true, LOGIC_RING_BITS + 1u);
        channel_config_set_write_increment(&dma_cfg, true);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_dreq(&dma_cfg, pio_get_dreq(pio0, s_sm_capture, false));
        channel_config_set_chain_to(&dma_cfg, s_dma_capture_reload);
        dma_channel_configure(s_dma_capture, &dma_cfg, &s_sample_buffer, &pio0->rxf[s_sm_capture], s_reload_counter,
                              false);
    }

    // DMA reload capture
    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_capture_reload);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
        channel_config_set_write_increment(&dma_cfg, false);
        channel_config_set_read_increment(&dma_cfg, false);
        dma_channel_configure(s_dma_capture_reload, &dma_cfg, &dma_hw->ch[s_dma_capture].al1_transfer_count_trig,
                              &s_reload_counter, 1u, false);
    }

    // DMA halt capture when counter pushes
    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_halt_capture);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
        channel_config_set_write_increment(&dma_cfg, false);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_dreq(&dma_cfg, pio_get_dreq(pio0, s_sm_counter, false));
        dma_channel_configure(s_dma_halt_capture, &dma_cfg, &pio0->ctrl, &s_pio_ctrl_halt, 1u, false);
    }

    // PIO capture
    s_offset_capture = pio_add_program(pio0, &capture_program);
    s_pio_config_capture = capture_program_get_default_config(s_offset_capture);
    sm_config_set_in_pins(&s_pio_config_capture, 0u);
    sm_config_set_in_shift(&s_pio_config_capture, false, true, LOGIC_CAPTURE_CHANNEL_COUNT);
    sm_config_set_clkdiv(&s_pio_config_capture, s_clk_div);
    pio_sm_init(pio0, s_sm_capture, s_offset_capture, &s_pio_config_capture);
    pio0->instr_mem[s_offset_capture] = pio_encode_in(pio_pins, LOGIC_CAPTURE_CHANNEL_COUNT);

    // PIO counter
    s_offset_counter = pio_add_program(pio0, &counter_program);
    s_pio_config_counter = counter_program_get_default_config(s_offset_counter);
    sm_config_set_in_pins(&s_pio_config_counter, 0u);
    sm_config_set_in_shift(&s_pio_config_counter, false, true, LOGIC_CAPTURE_CHANNEL_COUNT);
    sm_config_set_clkdiv(&s_pio_config_counter, s_clk_div);
    pio_sm_init(pio0, s_sm_counter, s_offset_counter, &s_pio_config_counter);
    pio_interrupt_clear(pio0, 1u);
    pio_set_irq1_source_enabled(pio0, (enum pio_interrupt_source)pis_interrupt1, true);
    irq_set_exclusive_handler(PIO0_IRQ_1, logic_capture_complete_handler);
    irq_set_enabled(PIO0_IRQ_1, true);

    trigger_gate->enabled = s_trigger_count > 0u;

    // PIO trigger mux
    if (trigger_gate->enabled) {
        s_offset_mux = pio_add_program(pio0, &mux_program);
        s_pio_config_mux = mux_program_get_default_config(s_offset_mux);
        sm_config_set_clkdiv(&s_pio_config_mux, 1.0f);
        pio_interrupt_clear(pio0, 0u);
        pio_set_irq0_source_enabled(pio0, (enum pio_interrupt_source)pis_interrupt0, true);
        pio_sm_init(pio0, s_sm_mux, s_offset_mux, &s_pio_config_mux);
        irq_set_exclusive_handler(PIO0_IRQ_0, logic_capture_trigger_handler);
        irq_set_enabled(PIO0_IRQ_0, true);
    }

    // DMA set counter
    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_set_counter);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
        channel_config_set_write_increment(&dma_cfg, false);
        channel_config_set_read_increment(&dma_cfg, false);
        if (trigger_gate->enabled) {
            channel_config_set_dreq(&dma_cfg, pio_get_dreq(pio0, s_sm_mux, true));
            dma_channel_configure(s_dma_set_counter, &dma_cfg, &pio0->txf[s_sm_counter], &s_post_trigger_samples, 1u,
                                  false);
        } else {
            dma_channel_configure(s_dma_set_counter, &dma_cfg, &pio0->txf[s_sm_counter], &s_reload_counter, 1u, false);
        }
    }

    trigger_gate->dreq = trigger_gate->enabled ? pio_get_dreq(pio0, s_sm_mux, false) : 0u;

    activation->pio0_enable_mask = trigger_gate->enabled
                                       ? (1u << s_sm_capture) | (1u << s_sm_mux) | (1u << s_sm_counter)
                                       : (1u << s_sm_capture) | (1u << s_sm_counter);
    activation->pio1_enable_mask = trigger_gate->enabled ? s_sm_trigger_mask : 0u;

    s_phase = LOGIC_CAPTURE_PHASE_ARMED;

    debug_block("\n[logic] prepare armed phase=%s samples=%u rate=%u pre=%u post=%u triggers=%u",
                logic_capture_phase_name(s_phase), s_pre_trigger_samples + s_post_trigger_samples, s_rate,
                s_pre_trigger_samples, s_post_trigger_samples, s_trigger_count);

    return true;
}

bool logic_capture_arm(void) {
    if (s_phase != LOGIC_CAPTURE_PHASE_ARMED) {
        debug("\n[logic] arm rejected reason=bad_phase phase=%s", logic_capture_phase_name(s_phase));
        return false;
    }

    if (s_activation_armed) {
        debug("\n[logic] arm rejected reason=already_armed");
        return false;
    }

    dma_channel_start(s_dma_capture);
    dma_channel_start(s_dma_set_counter);
    dma_channel_start(s_dma_halt_capture);

    if (s_sm_trigger_mask) {
        for (uint i = 0u; i < s_trigger_count; ++i) {
            dma_channel_start(s_dma_trigger_to_mux[i]);
        }
    }

    s_activation_armed = true;

    debug_block("\n[logic] arm ready phase=%s samples=%u rate=%u pre=%u post=%u triggers=%u",
                logic_capture_phase_name(s_phase), s_pre_trigger_samples + s_post_trigger_samples, s_rate,
                s_pre_trigger_samples, s_post_trigger_samples, s_trigger_count);

    return true;
}

void logic_capture_mark_capturing(void) {
    if (s_phase != LOGIC_CAPTURE_PHASE_ARMED) {
        return;
    }

    s_phase = LOGIC_CAPTURE_PHASE_CAPTURING;
    s_activation_armed = false;
}

void logic_capture_activate(const logic_capture_activation_t *activation) {
    if (s_phase != LOGIC_CAPTURE_PHASE_ARMED || !s_activation_armed || activation == NULL) {
        debug("\n[logic] activate ignored reason=bad_state phase=%s armed=%u activation=%u",
              logic_capture_phase_name(s_phase), s_activation_armed, activation != NULL);
        return;
    }

    pio0->ctrl = activation->pio0_enable_mask;
    if (activation->pio1_enable_mask) {
        pio1->ctrl = activation->pio1_enable_mask;
    }

    logic_capture_mark_capturing();

    debug_block("\n[logic] activate started phase=%s samples=%u rate=%u pre=%u post=%u triggers=%u",
                logic_capture_phase_name(s_phase), s_pre_trigger_samples + s_post_trigger_samples, s_rate,
                s_pre_trigger_samples, s_post_trigger_samples, s_trigger_count);
}

bool logic_capture_start(const capture_config_t *config, complete_handler_t handler) {
    capture_trigger_gate_t trigger_gate = {.enabled = false, .dreq = 0u};
    logic_capture_activation_t activation = {.pio0_enable_mask = 0u, .pio1_enable_mask = 0u};

    if (!logic_capture_prepare(config, handler, &trigger_gate, &activation)) {
        return false;
    }

    if (!logic_capture_arm()) {
        logic_capture_reset();
        return false;
    }

    logic_capture_activate(&activation);
    return true;
}

int logic_capture_get_triggered_channel(void) { return s_triggered_channel; }

bool logic_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len) {
    const uint32_t total_bytes = (s_pre_trigger_samples + s_post_trigger_samples) * sizeof(uint16_t);
    uint32_t remaining_bytes;
    uint16_t chunk_bytes;

    if (block_id == NULL || data == NULL || data_len == NULL) {
        debug("\n[logic] read rejected reason=bad_args");
        return false;
    }

    if (s_phase != LOGIC_CAPTURE_PHASE_FINALIZED) {
        debug("\n[logic] read rejected reason=not_finalized phase=%s", logic_capture_phase_name(s_phase));
        return false;
    }

    if (s_capture_read_offset_bytes >= total_bytes) {
        debug("\n[logic] read drained total_bytes=%lu", (unsigned long)total_bytes);
        return false;
    }

    remaining_bytes = total_bytes - s_capture_read_offset_bytes;
    chunk_bytes = (remaining_bytes > LOGIC_CAPTURE_BLOCK_BYTES) ? LOGIC_CAPTURE_BLOCK_BYTES : (uint16_t)remaining_bytes;

    {
        uint index = s_capture_read_offset_bytes / sizeof(uint16_t);

        for (uint i = 0u; i < chunk_bytes / sizeof(uint16_t); ++i) {
            const uint16_t sample = logic_capture_get_sample_index((int)index);
            data[i * 2u] = (uint8_t)(sample & 0xffu);
            data[i * 2u + 1u] = (uint8_t)((sample >> 8) & 0xffu);
            ++index;
        }
    }

    *block_id = (uint16_t)(s_capture_read_offset_bytes / LOGIC_CAPTURE_BLOCK_BYTES);
    *data_len = chunk_bytes;
    s_capture_read_offset_bytes += chunk_bytes;

    debug("\n[logic] read result block=%u data_len=%u next_offset=%lu pending=%s", *block_id, *data_len,
          (unsigned long)s_capture_read_offset_bytes, s_capture_read_offset_bytes < total_bytes ? "yes" : "no");

    return true;
}

capture_state_t logic_capture_get_state(void) {
    if (s_phase == LOGIC_CAPTURE_PHASE_ARMED || s_phase == LOGIC_CAPTURE_PHASE_CAPTURING) {
        return CAPTURE_RUNNING;
    }

    return CAPTURE_IDLE;
}