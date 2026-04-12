/*
 * PicoMSO - Mixed Signal Oscilloscope
 * Copyright (C) 2026 Daniel Gorbea
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

#include "scope_capture.h"

#include <string.h>

#include "debug.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#define SCOPE_CAPTURE_ADC_GPIO 26u

#define GPIO_COUPLING_CH1_DC 20u
#define GPIO_COUPLING_CH2_DC 21u

#define SCOPE_CAPTURE_RATE_CHANGE_CLK_HZ 500000u

typedef enum scope_capture_phase_t {
    SCOPE_CAPTURE_PHASE_DISARMED = 0,
    SCOPE_CAPTURE_PHASE_ARMED,
    SCOPE_CAPTURE_PHASE_CAPTURING,
    SCOPE_CAPTURE_PHASE_ABORTING,
    SCOPE_CAPTURE_PHASE_FINALIZED
} scope_capture_phase_t;

typedef enum scope_sample_width_t {
    SCOPE_SAMPLE_WIDTH_8 = 1,
    SCOPE_SAMPLE_WIDTH_12 = 2,
} scope_sample_width_t;

static capture_config_t s_scope_capture_config = {
    .total_samples = 0u,
    .rate = 0u,
    .pre_trigger_samples = 0u,
    .channels = 1u,
    .trigger =
        {
            {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
            {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
            {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
            {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
        },
};

static volatile scope_capture_phase_t s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
static uint32_t s_capture_read_offset_bytes = 0u;
static bool s_activation_armed = false;
static capture_trigger_gate_t s_trigger_gate = {.enabled = false, .dreq = 0u};

static const uint s_dma_capture = 6u;
static const uint s_dma_capture_reload = 7u;
static const uint s_dma_disable_adc = 8u;
static uint s_reload_counter;
static uint16_t s_sample_buffer[SCOPE_BUFFER_SIZE] __attribute__((aligned(SCOPE_BUFFER_SIZE * sizeof(uint16_t))));

static volatile uint32_t *s_clk_adc_ctrl = (volatile uint32_t *)(CLOCKS_BASE + CLOCKS_CLK_ADC_CTRL_OFFSET);
static void (*s_complete_handler)(void) = NULL;

static uint s_pre_trigger_samples;
static uint s_post_trigger_samples;
static uint s_rate;
static volatile int s_first_sample;
static scope_sample_width_t s_sample_width = SCOPE_SAMPLE_WIDTH_12;
static const uint s_adc_disable_mask = ADC_CS_EN_BITS;

static uint scope_channel_count(uint32_t channels_mask);
static const char *scope_capture_phase_name(scope_capture_phase_t phase);
static inline uint16_t scope_capture_get_sample_u8(int index);
static inline uint16_t scope_capture_get_sample_u16(int index);
static inline void scope_capture_complete_handler(void);
static inline void scope_capture_stop_hardware(void);
static void scope_capture_activate(void);
static void scope_capture_configure_adc(void);
static void scope_set_samplerate(uint samplerate);

static uint scope_channel_count(uint32_t channels_mask) {
    uint count = 0u;

    for (uint i = 0u; i < 32u; ++i) {
        if (channels_mask & (1u << i)) {
            ++count;
        }
    }

    return count;
}

static const char *scope_capture_phase_name(scope_capture_phase_t phase) {
    switch (phase) {
        case SCOPE_CAPTURE_PHASE_DISARMED:
            return "DISARMED";
        case SCOPE_CAPTURE_PHASE_ARMED:
            return "ARMED";
        case SCOPE_CAPTURE_PHASE_CAPTURING:
            return "CAPTURING";
        case SCOPE_CAPTURE_PHASE_ABORTING:
            return "ABORTING";
        case SCOPE_CAPTURE_PHASE_FINALIZED:
            return "FINALIZED";
        default:
            return "UNKNOWN";
    }
}

static inline uint16_t scope_capture_get_sample_u8(int index) {
    const uint total_samples = s_pre_trigger_samples + s_post_trigger_samples;
    const uint8_t *buffer = (const uint8_t *)s_sample_buffer;

    if (index < 0 || (uint)index >= total_samples) {
        return 0u;
    }

    int pos = s_first_sample + index * 2;

    if (pos < 0) {
        pos += SCOPE_BUFFER_SIZE;
    } else if (pos + 1 >= (int)SCOPE_BUFFER_SIZE) {
        pos -= SCOPE_BUFFER_SIZE;
    }

    return (uint16_t)(buffer[pos] | (buffer[pos + 1] << 8u));
}

static inline uint16_t scope_capture_get_sample_u16(int index) {
    const uint total_samples = s_pre_trigger_samples + s_post_trigger_samples;

    if (index < 0 || (uint)index >= total_samples) {
        return 0u;
    }

    int pos = s_first_sample + index;

    if (pos < 0) {
        pos += SCOPE_BUFFER_SIZE;
    } else if (pos >= (int)SCOPE_BUFFER_SIZE) {
        pos -= SCOPE_BUFFER_SIZE;
    }

    return s_sample_buffer[pos];
}

static void scope_capture_activate(void) {
    if (s_phase != SCOPE_CAPTURE_PHASE_ARMED || !s_activation_armed) {
        debug("\n[scope] activate ignored reason=bad_state phase=%s armed=%u", scope_capture_phase_name(s_phase),
              s_activation_armed);
        return;
    }

    adc_run(true);
    scope_capture_mark_capturing();

    debug("\n[scope] activate started phase=%s samples=%u rate=%u pre=%u post=%u gate=%s width=%u",
          scope_capture_phase_name(s_phase), s_scope_capture_config.total_samples, s_rate, s_pre_trigger_samples,
          s_post_trigger_samples, s_trigger_gate.enabled ? "logic" : "none", (unsigned)s_sample_width);
}

static void scope_capture_configure_adc(void) {
    adc_init();
    adc_gpio_init(SCOPE_CAPTURE_ADC_GPIO);
    adc_gpio_init(SCOPE_CAPTURE_ADC_GPIO + 1u);
    adc_gpio_init(SCOPE_CAPTURE_ADC_GPIO + 2u);

    gpio_init(GPIO_COUPLING_CH1_DC);
    gpio_set_dir(GPIO_COUPLING_CH1_DC, GPIO_OUT);
    gpio_put(GPIO_COUPLING_CH1_DC, false);

    gpio_init(GPIO_COUPLING_CH2_DC);
    gpio_set_dir(GPIO_COUPLING_CH2_DC, GPIO_OUT);
    gpio_put(GPIO_COUPLING_CH2_DC, false);

    /* Calibration PWM placeholder. */
    /* s_slice_num = pwm_gpio_to_slice_num(GPIO_CALIBRATION); */
}

static inline void scope_capture_complete_handler(void) {
    debug("\n[scope] dma irq entered ints0=%08lx phase=%s",
          (unsigned long)dma_hw->ints0, scope_capture_phase_name(s_phase), dma_hw->ints0);
    if (dma_hw->ints0 & (1u << s_dma_disable_adc)) {
        dma_hw->ints0 = 1u << s_dma_disable_adc;
        if (s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
            int pos = SCOPE_BUFFER_SIZE - dma_hw->ch[s_dma_capture].transfer_count - 6u;
            s_first_sample = pos - (s_pre_trigger_samples + s_post_trigger_samples);
            if (s_first_sample < 0) {
                s_first_sample += SCOPE_BUFFER_SIZE;
            }

            scope_capture_stop_hardware();
            s_phase = SCOPE_CAPTURE_PHASE_FINALIZED;

            debug("\n[scope] complete phase=%s width=%u", scope_capture_phase_name(s_phase), (unsigned)s_sample_width);

            if (s_complete_handler != NULL) {
                s_complete_handler();
            }
        } else {
            s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
            debug("\n[scope] complete ignored phase=%s", scope_capture_phase_name(s_phase));
        }
    }
}

static inline void scope_capture_stop_hardware(void) {
    dma_channel_abort(s_dma_capture);
    dma_channel_abort(s_dma_capture_reload);
    dma_channel_abort(s_dma_disable_adc);
}

static void scope_set_samplerate(uint samplerate) {
    float clk_div;
    uint channel_count;
    uint adc_rate;

    if (samplerate == 0u) {
        s_scope_capture_config.rate = 100000u;
        samplerate = 100000u;
    }

    channel_count = scope_channel_count(s_scope_capture_config.channels);
    if (channel_count == 0u) {
        channel_count = 1u;
    }

    adc_rate = samplerate * channel_count;

    if (adc_rate >= SCOPE_CAPTURE_RATE_CHANGE_CLK_HZ) {
        clk_div = (float)clock_get_hz(clk_sys) / (float)adc_rate - 1;
        *s_clk_adc_ctrl = 0x820u;
    } else {
        clk_div = (float)clock_get_hz(clk_usb) / (float)adc_rate - 1;
        *s_clk_adc_ctrl = 0x800u;
    }

    if (clk_div < 1.0f) {
        clk_div = 1.0f;
        debug("\n[scope] samplerate too high");
    }

    debug("\n[scope] samplerate configured requested_rate=%u adc_rate=%u channels=%u clk_div=%.2f source=%s",
          samplerate, adc_rate, channel_count, clk_div,
          adc_rate >= SCOPE_CAPTURE_RATE_CHANGE_CLK_HZ ? "clk_sys" : "clk_usb");

    adc_set_clkdiv(clk_div);
}

capture_state_t scope_capture_get_state(void) {
    if (s_phase == SCOPE_CAPTURE_PHASE_ARMED || s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
        return CAPTURE_RUNNING;
    }

    return CAPTURE_IDLE;
}

bool scope_capture_start(const capture_config_t *config, complete_handler_t handler) {
    capture_trigger_gate_t trigger_gate = {.enabled = false, .dreq = 0u};

    if (!scope_capture_prepare(config, handler, &trigger_gate)) {
        return false;
    }

    if (!scope_capture_arm()) {
        scope_capture_reset();
        return false;
    }

    scope_capture_activate();
    return true;
}

bool scope_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len) {
    const uint32_t total_bytes = (s_pre_trigger_samples + s_post_trigger_samples) * sizeof(uint16_t);
    uint32_t remaining_bytes;
    uint16_t chunk_bytes;

    if (block_id == NULL || data == NULL || data_len == NULL) {
        debug("\n[scope] read rejected reason=bad_args");
        return false;
    }

    if (s_phase != SCOPE_CAPTURE_PHASE_FINALIZED) {
        debug("\n[scope] read rejected reason=not_finalized phase=%s", scope_capture_phase_name(s_phase));
        return false;
    }

    if (s_capture_read_offset_bytes >= total_bytes) {
        debug("\n[scope] read drained total_bytes=%lu", (unsigned long)total_bytes);
        return false;
    }

    remaining_bytes = total_bytes - s_capture_read_offset_bytes;
    chunk_bytes = (remaining_bytes > SCOPE_CAPTURE_BLOCK_BYTES) ? SCOPE_CAPTURE_BLOCK_BYTES : (uint16_t)remaining_bytes;

    {
        uint index = s_capture_read_offset_bytes / sizeof(uint16_t);

        for (uint i = 0u; i < chunk_bytes / sizeof(uint16_t); ++i) {
            uint16_t sample;

            if (s_sample_width == SCOPE_SAMPLE_WIDTH_8) {
                sample = scope_capture_get_sample_u8((int)index);
            } else {
                sample = scope_capture_get_sample_u16((int)index);
            }

            data[i * 2u] = (uint8_t)(sample & 0xffu);
            data[i * 2u + 1u] = (uint8_t)((sample >> 8) & 0xffu);
            ++index;
        }
    }

    *block_id = (uint16_t)(s_capture_read_offset_bytes / SCOPE_CAPTURE_BLOCK_BYTES);
    *data_len = chunk_bytes;
    s_capture_read_offset_bytes += chunk_bytes;

    debug("\n[scope] read result block=%u data_len=%u next_offset=%lu pending=%s width=%u", *block_id, *data_len,
          (unsigned long)s_capture_read_offset_bytes, s_capture_read_offset_bytes < total_bytes ? "yes" : "no",
          (unsigned)s_sample_width);

    return true;
}

void scope_set_coupling(channel_t channel, coupling_t coupling) {
    if (channel == CHANNEL1) {
        gpio_put(GPIO_COUPLING_CH1_DC, coupling == COUPLING_DC);
    } else {
        gpio_put(GPIO_COUPLING_CH2_DC, coupling == COUPLING_DC);
    }
}

void scope_capture_reset(void) {
    debug("\n[scope] reset begin phase=%s total_samples=%lu read_offset=%lu", scope_capture_phase_name(s_phase),
          (unsigned long)s_scope_capture_config.total_samples, (unsigned long)s_capture_read_offset_bytes);

    if (s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
        s_phase = SCOPE_CAPTURE_PHASE_ABORTING;
        scope_capture_stop_hardware();
    }

    s_scope_capture_config.total_samples = 0u;
    s_scope_capture_config.rate = 0u;
    s_scope_capture_config.pre_trigger_samples = 0u;

    s_capture_read_offset_bytes = 0u;
    s_pre_trigger_samples = 0u;
    s_post_trigger_samples = 0u;
    s_activation_armed = false;
    s_trigger_gate.enabled = false;
    s_trigger_gate.dreq = 0u;
    s_sample_width = SCOPE_SAMPLE_WIDTH_12;
    s_phase = SCOPE_CAPTURE_PHASE_DISARMED;

    debug("\n[scope] reset done phase=%s", scope_capture_phase_name(s_phase));
}

bool scope_capture_prepare(const capture_config_t *config, complete_handler_t handler,
                           const capture_trigger_gate_t *trigger_gate) {
    if (config == NULL || handler == NULL || trigger_gate == NULL) {
        debug("\n[scope] prepare rejected reason=bad_args config=%u handler=%u gate=%u", config != NULL,
              handler != NULL, trigger_gate != NULL);
        return false;
    }

    if (s_phase != SCOPE_CAPTURE_PHASE_DISARMED && s_phase != SCOPE_CAPTURE_PHASE_FINALIZED) {
        debug("\n[scope] prepare rejected reason=busy phase=%s", scope_capture_phase_name(s_phase));
        return false;
    }

    if (config->total_samples == 0u || config->total_samples > SCOPE_CAPTURE_MAX_SAMPLES) {
        debug("\n[scope] prepare rejected reason=invalid_total_samples samples=%lu max=%u",
              (unsigned long)config->total_samples, SCOPE_CAPTURE_MAX_SAMPLES);
        return false;
    }

    if (config->pre_trigger_samples > config->total_samples) {
        debug("\n[scope] prepare rejected reason=pre_gt_total pre=%lu samples=%lu",
              (unsigned long)config->pre_trigger_samples, (unsigned long)config->total_samples);
        return false;
    }

    s_complete_handler = handler;
    s_scope_capture_config = *config;
    s_reload_counter = SCOPE_BUFFER_SIZE;

    if (s_scope_capture_config.channels == 0u) {
        s_scope_capture_config.channels = 1u;
    }

    s_sample_width =
        scope_channel_count(s_scope_capture_config.channels) > 1u ? SCOPE_SAMPLE_WIDTH_8 : SCOPE_SAMPLE_WIDTH_12;
    s_capture_read_offset_bytes = 0u;

    {
        const uint32_t samples = config->total_samples;
        const uint32_t rate = config->rate;
        const uint32_t pre_trigger_samples = config->pre_trigger_samples;

        s_trigger_gate = *trigger_gate;
        s_activation_armed = false;

        debug("\n[scope] prepare request phase=%s samples=%lu rate=%lu pre=%lu gate=%s width=%u",
              scope_capture_phase_name(s_phase), (unsigned long)samples, (unsigned long)rate,
              (unsigned long)pre_trigger_samples, s_trigger_gate.enabled ? "logic" : "none", (unsigned)s_sample_width);
    }

    scope_capture_configure_adc();

    uint channel_count = scope_channel_count(s_scope_capture_config.channels);
    if (channel_count == 0u) {
        channel_count = 1u;
    }

    s_pre_trigger_samples = config->pre_trigger_samples * channel_count;
    s_post_trigger_samples = (config->total_samples - config->pre_trigger_samples) * channel_count;
    s_rate = config->rate;
    s_first_sample = 0;

    adc_run(false);
    adc_fifo_drain();

    /* byte_shift = true for 8-bit dual-channel mode, false for 12-bit single-channel mode */
    adc_fifo_setup(true, true, 1u, false, s_sample_width == SCOPE_SAMPLE_WIDTH_8);

    scope_set_samplerate(s_scope_capture_config.rate);

    // DMA capture
    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_capture);
        if (s_sample_width == SCOPE_SAMPLE_WIDTH_8) {
            channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
            channel_config_set_ring(&dma_cfg, true, SCOPE_RING_BITS);
            channel_config_set_chain_to(&dma_cfg, s_dma_capture_reload);
        } else {
            channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
            channel_config_set_ring(&dma_cfg, true, SCOPE_RING_BITS + 1u);
            channel_config_set_chain_to(&dma_cfg, s_dma_capture_reload);
        }
        channel_config_set_write_increment(&dma_cfg, true);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_dreq(&dma_cfg, DREQ_ADC);
        dma_channel_configure(s_dma_capture, &dma_cfg, s_sample_buffer, &adc_hw->fifo, s_reload_counter, false);
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

    // DMA disable adc on complete
    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_disable_adc);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
        channel_config_set_write_increment(&dma_cfg, false);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_dreq(&dma_cfg, s_trigger_gate.dreq);
        irq_set_exclusive_handler(DMA_IRQ_0, scope_capture_complete_handler);
        irq_set_enabled(DMA_IRQ_0, true);
        dma_channel_set_irq0_enabled(s_dma_disable_adc, true);
        dma_channel_configure(s_dma_disable_adc, &dma_cfg, (io_rw_32 *)((uintptr_t)&adc_hw->cs + 0x3000),
                              &s_adc_disable_mask, 1u, false);
    }

    adc_set_round_robin(s_scope_capture_config.channels);

    s_phase = SCOPE_CAPTURE_PHASE_ARMED;

    debug("\n[scope] prepare armed phase=%s samples=%u rate=%u pre=%u post=%u gate=%s width=%u",
          scope_capture_phase_name(s_phase), s_scope_capture_config.total_samples, s_rate, s_pre_trigger_samples,
          s_post_trigger_samples, s_trigger_gate.enabled ? "logic" : "none", (unsigned)s_sample_width);

    return true;
}

bool scope_capture_arm(void) {
    if (s_phase != SCOPE_CAPTURE_PHASE_ARMED) {
        debug("\n[scope] arm rejected reason=bad_phase phase=%s", scope_capture_phase_name(s_phase));
        return false;
    }

    if (s_activation_armed) {
        debug("\n[scope] arm rejected reason=already_armed");
        return false;
    }

    dma_channel_start(s_dma_capture);
    dma_channel_start(s_dma_disable_adc);

    s_activation_armed = true;

    debug("\n[scope] arm ready phase=%s samples=%u rate=%u pre=%u post=%u gate=%s width=%u",
          scope_capture_phase_name(s_phase), s_scope_capture_config.total_samples, s_rate, s_pre_trigger_samples,
          s_post_trigger_samples, s_trigger_gate.enabled ? "logic" : "none", (unsigned)s_sample_width);

    return true;
}

void scope_capture_mark_capturing(void) {
    if (s_phase != SCOPE_CAPTURE_PHASE_ARMED) {
        return;
    }

    s_phase = SCOPE_CAPTURE_PHASE_CAPTURING;
    s_activation_armed = false;
}
