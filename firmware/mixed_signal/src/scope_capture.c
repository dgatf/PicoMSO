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

#include "scope_capture.h"

#include <string.h>

#include "debug.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#define SCOPE_CAPTURE_ADC_GPIO 26u
#define SCOPE_CAPTURE_ADC_INPUT 0u

#define SCOPE_PRE_TRIGGER_RING_BITS 12
#define SCOPE_PRE_TRIGGER_BUFFER_SIZE (1u << SCOPE_PRE_TRIGGER_RING_BITS)
#define SCOPE_POST_TRIGGER_BUFFER_SIZE 50000u
#define SCOPE_PRE_TRIGGER_RING_TRANSFER_COUNT \
    ((0xffffffffu / SCOPE_PRE_TRIGGER_BUFFER_SIZE) * SCOPE_PRE_TRIGGER_BUFFER_SIZE)

#define GPIO_COUPLING_CH1_DC 20u
#define GPIO_COUPLING_CH2_DC 21u
#define GPIO_CALIBRATION 22u

typedef enum scope_capture_phase_t {
    SCOPE_CAPTURE_PHASE_DISARMED = 0,
    SCOPE_CAPTURE_PHASE_ARMED,
    SCOPE_CAPTURE_PHASE_CAPTURING,
    SCOPE_CAPTURE_PHASE_ABORTING,
    SCOPE_CAPTURE_PHASE_FINALIZED
} scope_capture_phase_t;

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

static scope_capture_phase_t s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
static uint32_t s_capture_read_offset_bytes = 0u;
static bool s_activation_armed = false;
static capture_trigger_gate_t s_trigger_gate = {.enabled = false, .dreq = 0u};

static const uint s_dma_channel_adc = 7u;
static const uint s_dma_channel_adc_post = 8u;
static const uint s_dma_channel_reload_adc_counter = 9u;
static const uint s_dma_channel_dma_pre = 10u;
static const uint s_dma_channel_dma_post = 11u;
static const uint s_reload_counter = SCOPE_PRE_TRIGGER_RING_TRANSFER_COUNT;

static uint s_slice_num;
static uint16_t s_pre_trigger_buffer[SCOPE_PRE_TRIGGER_BUFFER_SIZE]
    __attribute__((aligned(SCOPE_PRE_TRIGGER_BUFFER_SIZE * sizeof(uint16_t))));
static uint16_t s_post_trigger_buffer[SCOPE_POST_TRIGGER_BUFFER_SIZE];

static volatile uint32_t *s_clk_adc_ctrl = (volatile uint32_t *)(CLOCKS_BASE + CLOCKS_CLK_ADC_CTRL_OFFSET);
static void (*s_complete_handler)(void) = NULL;

static volatile uint32_t s_dma_pre_mask = 1u << 7u;
static volatile uint32_t s_dma_post_mask = 1u << 8u;

static volatile uint s_pre_trigger_count;
static uint s_pre_trigger_samples;
static uint s_post_trigger_samples;
static uint s_rate;
static int s_pre_trigger_first;

static void scope_capture_configure_adc(void);
static inline void scope_capture_complete_handler(void);
static inline void scope_capture_stop_hardware(void);

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
    s_pre_trigger_count = 0u;
    s_pre_trigger_first = 0;
    s_activation_armed = false;
    s_trigger_gate.enabled = false;
    s_trigger_gate.dreq = 0u;
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
    s_scope_capture_config.channels = 1u;
    s_capture_read_offset_bytes = 0u;

    {
        const uint32_t samples = config->total_samples;
        const uint32_t rate = config->rate;
        const uint32_t pre_trigger_samples = config->pre_trigger_samples;

        s_trigger_gate = *trigger_gate;
        s_activation_armed = false;

        debug("\n[scope] prepare request phase=%s samples=%lu rate=%lu pre=%lu gate=%s",
              scope_capture_phase_name(s_phase), (unsigned long)samples, (unsigned long)rate,
              (unsigned long)pre_trigger_samples, s_trigger_gate.enabled ? "logic" : "none");
    }

    scope_capture_configure_adc();

    s_pre_trigger_samples = config->pre_trigger_samples;
    s_post_trigger_samples = config->total_samples - config->pre_trigger_samples;
    s_rate = config->rate;
    s_pre_trigger_count = 0u;
    s_pre_trigger_first = 0;

    adc_run(false);
    adc_fifo_drain();
    adc_fifo_setup(true, true, 1u, false, false);

    oscilloscope_set_samplerate(s_scope_capture_config.rate);

    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_channel_reload_adc_counter);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
        channel_config_set_write_increment(&dma_cfg, false);
        channel_config_set_read_increment(&dma_cfg, false);
        dma_channel_configure(s_dma_channel_reload_adc_counter, &dma_cfg,
                              &dma_hw->ch[s_dma_channel_adc].al1_transfer_count_trig, &s_reload_counter, 1u, false);
    }

    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_channel_adc);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
        channel_config_set_ring(&dma_cfg, true, SCOPE_PRE_TRIGGER_RING_BITS);
        channel_config_set_write_increment(&dma_cfg, true);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_dreq(&dma_cfg, DREQ_ADC);
        channel_config_set_chain_to(&dma_cfg, s_dma_channel_reload_adc_counter);
        dma_channel_configure(s_dma_channel_adc, &dma_cfg, &s_pre_trigger_buffer, &adc_hw->fifo,
                              SCOPE_PRE_TRIGGER_RING_TRANSFER_COUNT, false);
    }

    {
        dma_channel_config dma_cfg = dma_channel_get_default_config(s_dma_channel_adc_post);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16);
        channel_config_set_write_increment(&dma_cfg, true);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_dreq(&dma_cfg, DREQ_ADC);
        dma_channel_set_irq1_enabled(s_dma_channel_adc_post, true);
        dma_channel_configure(s_dma_channel_adc_post, &dma_cfg, &s_post_trigger_buffer, &adc_hw->fifo,
                              s_post_trigger_samples, false);
    }

    if (s_trigger_gate.enabled) {
        dma_channel_config dma_cfg_pre = dma_channel_get_default_config(s_dma_channel_dma_pre);
        dma_channel_config dma_cfg_post = dma_channel_get_default_config(s_dma_channel_dma_post);

        channel_config_set_transfer_data_size(&dma_cfg_pre, DMA_SIZE_32);
        channel_config_set_write_increment(&dma_cfg_pre, false);
        channel_config_set_read_increment(&dma_cfg_pre, false);
        channel_config_set_dreq(&dma_cfg_pre, s_trigger_gate.dreq);
        channel_config_set_chain_to(&dma_cfg_pre, s_dma_channel_dma_post);
        dma_channel_configure(s_dma_channel_dma_pre, &dma_cfg_pre, &dma_hw->abort, &s_dma_pre_mask, 1u, false);

        channel_config_set_transfer_data_size(&dma_cfg_post, DMA_SIZE_32);
        channel_config_set_write_increment(&dma_cfg_post, false);
        channel_config_set_read_increment(&dma_cfg_post, false);
        dma_channel_configure(s_dma_channel_dma_post, &dma_cfg_post, &dma_hw->multi_channel_trigger, &s_dma_post_mask,
                              1u, false);
    }

    irq_set_exclusive_handler(DMA_IRQ_1, scope_capture_complete_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    adc_set_round_robin(s_scope_capture_config.channels);

    s_phase = SCOPE_CAPTURE_PHASE_ARMED;

    debug("\n[scope] prepare armed phase=%s samples=%u rate=%u pre=%u post=%u gate=%s",
          scope_capture_phase_name(s_phase), s_scope_capture_config.total_samples, s_rate, s_pre_trigger_samples,
          s_post_trigger_samples, s_trigger_gate.enabled ? "logic" : "none");

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

    if (!s_trigger_gate.enabled) {
        dma_hw->multi_channel_trigger = s_dma_post_mask;
    } else {
        dma_channel_start(s_dma_channel_dma_pre);
        dma_hw->multi_channel_trigger = s_dma_pre_mask;
    }

    s_activation_armed = true;

    debug("\n[scope] arm ready phase=%s samples=%u rate=%u pre=%u post=%u gate=%s", scope_capture_phase_name(s_phase),
          s_scope_capture_config.total_samples, s_rate, s_pre_trigger_samples, s_post_trigger_samples,
          s_trigger_gate.enabled ? "logic" : "none");

    return true;
}

void scope_capture_mark_capturing(void) {
    if (s_phase != SCOPE_CAPTURE_PHASE_ARMED) {
        return;
    }

    s_phase = SCOPE_CAPTURE_PHASE_CAPTURING;
    s_activation_armed = false;
}

void scope_capture_activate(void) {
    if (s_phase != SCOPE_CAPTURE_PHASE_ARMED || !s_activation_armed) {
        debug("\n[scope] activate ignored reason=bad_state phase=%s armed=%u", scope_capture_phase_name(s_phase),
              s_activation_armed);
        return;
    }

    adc_run(true);
    scope_capture_mark_capturing();

    debug("\n[scope] activate started phase=%s samples=%u rate=%u pre=%u post=%u gate=%s",
          scope_capture_phase_name(s_phase), s_scope_capture_config.total_samples, s_rate, s_pre_trigger_samples,
          s_post_trigger_samples, s_trigger_gate.enabled ? "logic" : "none");
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

uint16_t scope_capture_get_sample_index(int index) {
    const uint total_samples = s_pre_trigger_count + s_post_trigger_samples;

    if (index < 0 || (uint)index >= total_samples) {
        return 0u;
    }

    if (index < s_pre_trigger_count) {
        int pos = s_pre_trigger_first + (int)index;

        if (pos < 0) {
            pos += SCOPE_PRE_TRIGGER_BUFFER_SIZE;
        } else if (pos >= (int)SCOPE_PRE_TRIGGER_BUFFER_SIZE) {
            pos -= SCOPE_PRE_TRIGGER_BUFFER_SIZE;
        }

        return s_pre_trigger_buffer[pos];
    }

    return s_post_trigger_buffer[index - s_pre_trigger_count];
}

capture_state_t scope_capture_get_state(void) {
    if (s_phase == SCOPE_CAPTURE_PHASE_ARMED || s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
        return CAPTURE_RUNNING;
    }

    return CAPTURE_IDLE;
}

bool scope_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len) {
    const uint32_t total_bytes = scope_capture_get_samples_count() * sizeof(uint16_t);
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
            const uint16_t sample = scope_capture_get_sample_index((int)index);
            data[i * 2u] = (uint8_t)(sample & 0xffu);
            data[i * 2u + 1u] = (uint8_t)((sample >> 8) & 0xffu);
            ++index;
        }
    }

    *block_id = (uint16_t)(s_capture_read_offset_bytes / SCOPE_CAPTURE_BLOCK_BYTES);
    *data_len = chunk_bytes;
    s_capture_read_offset_bytes += chunk_bytes;

    debug("\n[scope] read result block=%u data_len=%u next_offset=%lu pending=%s", *block_id, *data_len,
          (unsigned long)s_capture_read_offset_bytes, s_capture_read_offset_bytes < total_bytes ? "yes" : "no");

    return true;
}

void oscilloscope_set_coupling(channel_t channel, coupling_t coupling) {
    if (channel == CHANNEL1) {
        gpio_put(GPIO_COUPLING_CH1_DC, coupling == COUPLING_DC);
    } else {
        gpio_put(GPIO_COUPLING_CH2_DC, coupling == COUPLING_DC);
    }
}

void oscilloscope_set_samplerate(uint samplerate) {
    float clk_div;

    if (samplerate == 0u) {
        s_scope_capture_config.rate = 100000u;
        samplerate = 100000u;
    }

    if (samplerate > 500000u) {
        clk_div = (float)clock_get_hz(clk_sys) / (float)samplerate - 1;
        *s_clk_adc_ctrl = 0x820u;  // clk_sys
    } else {
        clk_div = (float)clock_get_hz(clk_usb) / (float)samplerate - 1;
        *s_clk_adc_ctrl = 0x800u;  // clk_usb
    }

    if (clk_div < 1.0f) {
        clk_div = 1.0f;
        debug("\n[scope] samplerate too high");
    }

    debug("\n[scope] samplerate configured clk_div=%.2f rate=%u source=%s", clk_div, samplerate,
          samplerate > 500000u ? "clk_sys" : "clk_usb");

    adc_set_clkdiv(clk_div);
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

    // Calibration PWM placeholder.
    // s_slice_num = pwm_gpio_to_slice_num(GPIO_CALIBRATION);
}

uint scope_capture_get_samples_count(void) { return s_pre_trigger_count + s_post_trigger_samples; }

static inline void scope_capture_complete_handler(void) {
    dma_hw->ints1 = 1u << s_dma_channel_adc_post;

    if (s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
        s_pre_trigger_first = 0;
        s_pre_trigger_count = 0u;

        if (s_pre_trigger_samples != 0u) {
            uint transfer_count = SCOPE_PRE_TRIGGER_RING_TRANSFER_COUNT - dma_hw->ch[s_dma_channel_adc].transfer_count;

            s_pre_trigger_first = (int)(transfer_count % SCOPE_PRE_TRIGGER_BUFFER_SIZE) - (int)s_pre_trigger_samples;
            s_pre_trigger_count = s_pre_trigger_samples;

            if (s_pre_trigger_first < 0 && transfer_count < SCOPE_PRE_TRIGGER_BUFFER_SIZE) {
                s_pre_trigger_first = 0;
                s_pre_trigger_count = transfer_count;
            }
        }

        scope_capture_stop_hardware();
        s_phase = SCOPE_CAPTURE_PHASE_FINALIZED;

        debug("\n[scope] complete phase=%s pre_count=%u post_count=%u total_samples=%u pending_bytes=%lu",
              scope_capture_phase_name(s_phase), s_pre_trigger_count, s_post_trigger_samples,
              s_pre_trigger_count + s_post_trigger_samples,
              (unsigned long)(scope_capture_get_samples_count() * sizeof(uint16_t)));

        if (s_complete_handler != NULL) {
            s_complete_handler();
        }
    } else {
        s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
        debug("\n[scope] complete ignored phase=%s", scope_capture_phase_name(s_phase));
    }
}

static inline void scope_capture_stop_hardware(void) {
    adc_run(false);
    adc_fifo_drain();

    dma_channel_abort(s_dma_channel_adc);
    dma_channel_abort(s_dma_channel_adc_post);
    dma_channel_abort(s_dma_channel_reload_adc_counter);
    dma_channel_abort(s_dma_channel_dma_pre);
    dma_channel_abort(s_dma_channel_dma_post);
}