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
#include "logic_capture.h"
#include "pico/stdlib.h"

#define SCOPE_CAPTURE_ADC_GPIO 26u
#define SCOPE_CAPTURE_ADC_INPUT 0u

#define PRE_TRIGGER_RING_BITS 10
#define PRE_TRIGGER_BUFFER_SIZE (1 << PRE_TRIGGER_RING_BITS)
#define PRE_TRIGGER_RING_TRANSFER_COUNT ((0xffffffffu / PRE_TRIGGER_BUFFER_SIZE) * PRE_TRIGGER_BUFFER_SIZE)
#define POST_TRIGGER_BUFFER_SIZE 10000

#define GPIO_COUPLING_CH1_DC 20
#define GPIO_COUPLING_CH2_DC 21
#define GPIO_CALIBRATION 22

typedef enum scope_capture_phase_t {
    SCOPE_CAPTURE_PHASE_DISARMED = 0,
    SCOPE_CAPTURE_PHASE_CAPTURING,
    SCOPE_CAPTURE_PHASE_ABORTING,
    SCOPE_CAPTURE_PHASE_FINALIZED
} scope_capture_phase_t;

static capture_config_t s_scope_capture_config = {.total_samples = 0u,
                                                  .rate = 0u,
                                                  .pre_trigger_samples = 0u,
                                                  .channels = 1u,
                                                  .trigger = {
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                  }};

static scope_capture_phase_t s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
static uint32_t s_capture_read_offset_bytes = 0u;

static const uint dma_channel_adc_ = 7, dma_channel_adc_post_ = 8, dma_channel_reload_adc_counter_ = 9,
                  dma_channel_dma_pre_ = 10, dma_channel_dma_post_ = 11, reload_counter_ = 0xffffffffu;
static uint slice_num_;
static uint16_t pre_trigger_buffer_[PRE_TRIGGER_BUFFER_SIZE]
    __attribute__((aligned(PRE_TRIGGER_BUFFER_SIZE * sizeof(uint16_t)))),
    post_trigger_buffer_[POST_TRIGGER_BUFFER_SIZE];
static volatile uint32_t *clk_adc_ctrl = (volatile uint32_t *)(CLOCKS_BASE + CLOCKS_CLK_ADC_CTRL_OFFSET);
static void (*handler_)(void) = NULL;
static volatile uint32_t dma_pre_ = 1 << dma_channel_adc_, dma_post_ = 1 << dma_channel_adc_post_;
static volatile uint pre_trigger_count_;
static uint pre_trigger_samples_, post_trigger_samples_, rate_;
static int pre_trigger_first_;

static void scope_capture_configure_adc(void);
static inline void complete_handler(void);
static inline void capture_stop(void);

static const char *scope_capture_phase_name(scope_capture_phase_t phase)
{
    switch (phase) {
        case SCOPE_CAPTURE_PHASE_DISARMED:
            return "DISARMED";
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
    debug("\n[scope] reset begin phase=%s total_samples=%lu read_offset=%lu",
          scope_capture_phase_name(s_phase),
          (unsigned long)s_scope_capture_config.total_samples,
          (unsigned long)s_capture_read_offset_bytes);
    if (s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
        s_phase = SCOPE_CAPTURE_PHASE_ABORTING;
        capture_stop();
    }
    s_scope_capture_config.total_samples = 0u;
    s_scope_capture_config.rate = 0u;
    s_scope_capture_config.pre_trigger_samples = 0u;
    s_capture_read_offset_bytes = 0u;
    pre_trigger_samples_ = 0u;
    post_trigger_samples_ = 0u;
    pre_trigger_count_ = 0u;
    pre_trigger_first_ = 0;
    s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
    debug("\n[scope] reset done phase=%s", scope_capture_phase_name(s_phase));
}

bool scope_capture_start(const capture_config_t *config, complete_handler_t handler) {
    if (config == NULL || handler == NULL) {
        debug("\n[scope] start rejected reason=bad_args config=%u handler=%u",
              config != NULL, handler != NULL);
        return false;
    }

    if (config->total_samples == 0u || config->total_samples > SCOPE_CAPTURE_MAX_SAMPLES) {
        debug("\n[scope] start rejected reason=invalid_total_samples samples=%lu max=%u",
              (unsigned long)config->total_samples, SCOPE_CAPTURE_MAX_SAMPLES);
        return false;
    }

    if (config->pre_trigger_samples > config->total_samples) {
        debug("\n[scope] start rejected reason=pre_gt_total pre=%lu samples=%lu",
              (unsigned long)config->pre_trigger_samples,
              (unsigned long)config->total_samples);
        return false;
    }

    handler_ = handler;
    s_scope_capture_config = *config;
    s_scope_capture_config.channels = 1u;
    s_capture_read_offset_bytes = 0u;
    uint32_t samples = config->total_samples;
    uint32_t rate = config->rate;
    uint32_t pre_trigger_samples = config->pre_trigger_samples;

    debug("\n[scope] start request phase=%s samples=%lu rate=%lu pre=%lu logic_triggers=%u",
          scope_capture_phase_name(s_phase),
          (unsigned long)samples,
          (unsigned long)rate,
          (unsigned long)pre_trigger_samples,
          logic_capture_get_trigger_count());

    scope_capture_configure_adc();
    s_phase = SCOPE_CAPTURE_PHASE_CAPTURING;
    pre_trigger_samples_ = pre_trigger_samples;
    post_trigger_samples_ = samples - pre_trigger_samples;
    rate_ = rate;

    // adc setup
    adc_run(false);
    adc_fifo_drain();
    adc_fifo_setup(true,   // write to FIFO
                   true,   // enable DMA DREQ
                   1,      // assert DREQ (and IRQ) at least 1 sample present
                   false,  // omit ERR bit (bit 15) since we have 8 bit reads.
                   false   // 12 bit resolution
    );
    uint ch_count = 1;  //(oscilloscope_config_.channel_mask == 0b11) ? 2 : 1;
    oscilloscope_set_samplerate(s_scope_capture_config.rate * ch_count);

    // DMA channel ADC reload counter
    dma_channel_config config_dma_channel_reload_adc_counter =
        dma_channel_get_default_config(dma_channel_reload_adc_counter_);
    channel_config_set_transfer_data_size(&config_dma_channel_reload_adc_counter, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_channel_reload_adc_counter, false);
    channel_config_set_read_increment(&config_dma_channel_reload_adc_counter, false);
    dma_channel_configure(dma_channel_reload_adc_counter_, &config_dma_channel_reload_adc_counter,
                          &dma_hw->ch[dma_channel_adc_].al1_transfer_count_trig,  // write address
                          &reload_counter_,                                       // read address
                          1, false);

    // DMA channel ADC pre
    dma_channel_config channel_config_adc = dma_channel_get_default_config(dma_channel_adc_);
    channel_config_set_transfer_data_size(&channel_config_adc, DMA_SIZE_16);
    channel_config_set_ring(&channel_config_adc, true, PRE_TRIGGER_RING_BITS);
    channel_config_set_write_increment(&channel_config_adc, true);
    channel_config_set_read_increment(&channel_config_adc, false);
    channel_config_set_dreq(&channel_config_adc, DREQ_ADC);
    channel_config_set_chain_to(&channel_config_adc,
                                dma_channel_reload_adc_counter_);  // reload counter when completed
    dma_channel_configure(dma_channel_adc_, &channel_config_adc,
                          &pre_trigger_buffer_,  // write address
                          &adc_hw->fifo,         // read address
                          0xffffffffu, false);

    // DMA channel ADC post
    dma_channel_config channel_config_adc_post = dma_channel_get_default_config(dma_channel_adc_post_);
    channel_config_set_transfer_data_size(&channel_config_adc_post, DMA_SIZE_16);
    channel_config_set_write_increment(&channel_config_adc_post, true);
    channel_config_set_read_increment(&channel_config_adc_post, false);
    channel_config_set_dreq(&channel_config_adc_post, DREQ_ADC);
    dma_channel_set_irq0_enabled(dma_channel_adc_post_, true);  // raise an interrupt when completed
    dma_channel_configure(dma_channel_adc_post_, &channel_config_adc_post,
                          &post_trigger_buffer_,  // write address
                          &adc_hw->fifo,          // read address
                          s_scope_capture_config.total_samples - s_scope_capture_config.pre_trigger_samples, false);

    // DMA channel dma control: disable pre trigger and enable post trigger
    dma_channel_config config_dma_channel_dma_pre = dma_channel_get_default_config(dma_channel_dma_pre_);
    dma_channel_config config_dma_channel_dma_post = dma_channel_get_default_config(dma_channel_dma_post_);
    channel_config_set_transfer_data_size(&config_dma_channel_dma_pre, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_channel_dma_pre, false);
    channel_config_set_read_increment(&config_dma_channel_dma_pre, false);
    channel_config_set_dreq(&config_dma_channel_dma_pre, pio_get_dreq(pio0, logic_capture_get_sm_mux(), false));
    channel_config_set_chain_to(&config_dma_channel_dma_pre, dma_channel_dma_post_);
    dma_channel_configure(dma_channel_dma_pre_, &config_dma_channel_dma_pre,
                          &dma_hw->multi_channel_trigger,  // write address
                          &dma_pre_,                       // read address
                          1, false);
    channel_config_set_transfer_data_size(&config_dma_channel_dma_post, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_channel_dma_post, false);
    channel_config_set_read_increment(&config_dma_channel_dma_post, false);
    dma_channel_configure(dma_channel_dma_post_, &config_dma_channel_dma_post,
                          &dma_hw->abort,  // write address
                          &dma_post_,      // read address
                          1, false);
    irq_set_exclusive_handler(DMA_IRQ_0, complete_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    adc_set_round_robin(s_scope_capture_config.channels);

    if (!logic_capture_get_trigger_count()) {
        dma_hw->multi_channel_trigger = dma_post_;  // trigger post immediately if no logic triggers
    } else {
        dma_hw->multi_channel_trigger = dma_pre_;
    }

    adc_run(true);

    debug("\n[scope] start armed phase=%s samples=%u rate=%u pre=%u post=%u logic_triggers=%u",
          scope_capture_phase_name(s_phase), s_scope_capture_config.total_samples,
          rate_, pre_trigger_samples_, post_trigger_samples_, logic_capture_get_trigger_count());

    return true;
}

uint16_t scope_capture_get_sample_index(int index) {
    uint total_samples = pre_trigger_count_ + post_trigger_samples_;

    if (index < 0 || (uint)index >= total_samples) return 0;

    if ((uint)index < pre_trigger_count_) {
        int pos = pre_trigger_first_ + index;

        if (pos < 0)
            pos += PRE_TRIGGER_BUFFER_SIZE;
        else if (pos >= PRE_TRIGGER_BUFFER_SIZE)
            pos -= PRE_TRIGGER_BUFFER_SIZE;

        return pre_trigger_buffer_[pos];
    }

    return post_trigger_buffer_[index - (int)pre_trigger_count_];
}

capture_state_t scope_capture_get_state(void) {
    if (s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
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
        debug("\n[scope] read rejected reason=not_finalized phase=%s",
              scope_capture_phase_name(s_phase));
        return false;
    }

    if (s_capture_read_offset_bytes >= total_bytes) {
        debug("\n[scope] read drained total_bytes=%lu",
              (unsigned long)total_bytes);
        return false;
    }

    remaining_bytes = total_bytes - s_capture_read_offset_bytes;
    chunk_bytes = (remaining_bytes > SCOPE_CAPTURE_BLOCK_BYTES) ? SCOPE_CAPTURE_BLOCK_BYTES : (uint16_t)remaining_bytes;

    uint index = s_capture_read_offset_bytes / sizeof(uint16_t);
    for (uint i = 0; i < chunk_bytes / sizeof(uint16_t); ++i) {
        const uint16_t sample = scope_capture_get_sample_index(index);
        data[i * 2u] = (uint8_t)(sample & 0xFFu);
        data[i * 2u + 1u] = (uint8_t)((sample >> 8) & 0xFFu);
        ++index;
    }
    *block_id = (uint16_t)(s_capture_read_offset_bytes / SCOPE_CAPTURE_BLOCK_BYTES);
    *data_len = chunk_bytes;
    s_capture_read_offset_bytes += chunk_bytes;

    debug("\n[scope] read result block=%u data_len=%u next_offset=%lu pending=%s",
          *block_id, *data_len, (unsigned long)s_capture_read_offset_bytes,
          s_capture_read_offset_bytes < total_bytes ? "yes" : "no");

    return true;
}

void oscilloscope_set_coupling(channel_t channel, coupling_t coupling) {
    if (channel == CHANNEL1) {
        if (coupling == COUPLING_DC)
            gpio_put(GPIO_COUPLING_CH1_DC, true);
        else
            gpio_put(GPIO_COUPLING_CH1_DC, false);
    } else {
        if (coupling == COUPLING_DC)
            gpio_put(GPIO_COUPLING_CH2_DC, true);
        else
            gpio_put(GPIO_COUPLING_CH2_DC, false);
    }
}

void oscilloscope_set_samplerate(uint samplerate) {
    float clk_div;
    if (!samplerate) {
        s_scope_capture_config.rate = 100000;
        samplerate = 100000;
    }
    if (samplerate > 500e3) {
        clk_div = clock_get_hz(clk_sys) / samplerate;
        *clk_adc_ctrl = 0x820;  // clk_sys}
    } else {
        clk_div = clock_get_hz(clk_usb) / samplerate;
        *clk_adc_ctrl = 0x800;  // clk_usb
    }
    debug("\n[scope] samplerate configured clk_div=%.2f rate=%u source=%s",
          clk_div, samplerate, samplerate > 500e3 ? "clk_sys" : "clk_usb");
    adc_set_clkdiv(clk_div);
}

static void scope_capture_configure_adc(void) {
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);

    gpio_init(GPIO_COUPLING_CH1_DC);
    gpio_set_dir(GPIO_COUPLING_CH1_DC, GPIO_OUT);
    gpio_put(GPIO_COUPLING_CH1_DC, false);

    gpio_init(GPIO_COUPLING_CH2_DC);
    gpio_set_dir(GPIO_COUPLING_CH2_DC, GPIO_OUT);
    gpio_put(GPIO_COUPLING_CH2_DC, false);

    // calibration pwm
    // oscilloscope_config_.calibration_freq = 1000;
    // gpio_set_function(GPIO_CALIBRATION, GPIO_FUNC_PWM);
    // slice_num_ = pwm_gpio_to_slice_num(GPIO_CALIBRATION);
}

uint scope_capture_get_samples_count(void) { return pre_trigger_count_ + post_trigger_samples_; }

static inline void complete_handler(void) {
    dma_hw->ints0 = 1u << dma_channel_adc_post_;
    if (s_phase == SCOPE_CAPTURE_PHASE_CAPTURING) {
        // Set pre trigger range
        pre_trigger_first_ = 0;
        pre_trigger_count_ = 0;
        if (pre_trigger_samples_) {
            uint transfer_count = PRE_TRIGGER_RING_TRANSFER_COUNT - dma_hw->ch[dma_channel_adc_].transfer_count;
            pre_trigger_first_ = (int)(transfer_count % PRE_TRIGGER_BUFFER_SIZE) - (int)pre_trigger_samples_;
            pre_trigger_count_ = pre_trigger_samples_;
            if ((pre_trigger_first_ < 0) && (transfer_count < PRE_TRIGGER_BUFFER_SIZE)) {
                pre_trigger_first_ = 0;
                pre_trigger_count_ = transfer_count;
            }
        }
        capture_stop();
        s_phase = SCOPE_CAPTURE_PHASE_FINALIZED;
        debug("\n[scope] complete phase=%s pre_count=%u post_count=%u total_samples=%u pending_bytes=%lu",
              scope_capture_phase_name(s_phase), pre_trigger_count_, post_trigger_samples_,
              pre_trigger_count_ + post_trigger_samples_,
              (unsigned long)(scope_capture_get_samples_count() * sizeof(uint16_t)));
        if (handler_) handler_();
    } else {
        s_phase = SCOPE_CAPTURE_PHASE_DISARMED;
        debug("\n[scope] complete ignored phase=%s", scope_capture_phase_name(s_phase));
    }
}

static inline void capture_stop(void) {
    dma_channel_abort(dma_channel_adc_);
    dma_channel_abort(dma_channel_adc_post_);
    dma_channel_abort(dma_channel_reload_adc_counter_);
    dma_channel_abort(dma_channel_dma_pre_);
    dma_channel_abort(dma_channel_dma_post_);
}
