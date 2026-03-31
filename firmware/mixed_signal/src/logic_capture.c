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

#include "logic_capture.h"

#include <string.h>

#include "capture.pio.h"
#include "debug.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#define LOGIC_CAPTURE_CHANNELS 16u
#define LOGIC_CAPTURE_TRIGGER_TIMEOUT_SPINS 4096u

#define PRE_TRIGGER_RING_BITS 10
#define PRE_TRIGGER_BUFFER_SIZE (1 << PRE_TRIGGER_RING_BITS)
#define PRE_TRIGGER_RING_TRANSFER_COUNT ((0xffffffffu / PRE_TRIGGER_BUFFER_SIZE) * PRE_TRIGGER_BUFFER_SIZE)
#define POST_TRIGGER_BUFFER_SIZE 10000
#define MAX_TRIGGER_COUNT 4
#define RATE_CHANGE_CLK 5000

typedef enum logic_capture_phase_t {
    LOGIC_CAPTURE_PHASE_DISARMED = 0,
    LOGIC_CAPTURE_PHASE_CAPTURING,
    LOGIC_CAPTURE_PHASE_ABORTING,
    LOGIC_CAPTURE_PHASE_FINALIZED
} logic_capture_phase_t;

static capture_config_t s_logic_capture_config = {.total_samples = 0u,
                                                  .rate = 0u,
                                                  .pre_trigger_samples = 0u,
                                                  .channels = LOGIC_CAPTURE_CHANNELS,
                                                  .trigger = {
                                                      {.is_enabled = true, .pin = 0u, .match = TRIGGER_TYPE_EDGE_HIGH},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                  }};

static volatile logic_capture_phase_t s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
static uint32_t s_capture_read_offset_bytes = 0u;

static const uint sm_pre_trigger_ = 0, sm_post_trigger_ = 1, sm_mux_ = 3, dma_channel_pre_trigger_ = 0,
                  dma_channel_post_trigger_ = 1, dma_channel_pio0_ctrl_ = 2, dma_channel_pio1_ctrl_ = 3,
                  dma_channel_reload_pre_trigger_counter_ = 4, dma_channel_trigger_[MAX_TRIGGER_COUNT] = {5, 6, 7, 8},
                  sm_trigger_[MAX_TRIGGER_COUNT] = {0, 1, 2, 3}, reload_counter_ = PRE_TRIGGER_RING_TRANSFER_COUNT;
static uint offset_pre_trigger_, offset_post_trigger_, pre_trigger_samples_, post_trigger_samples_,
    pin_count_ = LOGIC_CAPTURE_CHANNELS, trigger_count_, sm_trigger_mask_, trigger_mask_, pin_base_, rate_, offset_mux_,
    offset_trigger_[MAX_TRIGGER_COUNT];
static volatile uint pre_trigger_count_;
static volatile int pre_trigger_first_, triggered_channel_;
static float clk_div_;
static volatile uint pio0_ctrl_ = (1 << sm_post_trigger_), pio1_ctrl_ = 0;
static uint16_t pre_trigger_buffer_[PRE_TRIGGER_BUFFER_SIZE]
    __attribute__((aligned(PRE_TRIGGER_BUFFER_SIZE * sizeof(uint16_t)))),
    post_trigger_buffer_[POST_TRIGGER_BUFFER_SIZE];
static pio_sm_config pio_config_trigger_[MAX_TRIGGER_COUNT], pio_config_pre_trigger_, pio_config_post_trigger_,
    pio_config_mux_;
static const uint triggered_channel_index_[4] = {0, 1, 2, 3};

static void (*handler_)(void) = NULL;

static void configure_inputs(void);
static inline void trigger_handler(void);
static inline bool set_trigger(trigger_t trigger);
static inline void capture_complete_handler(void);
static inline void capture_stop(void);

void logic_capture_reset(void) {
    if (clock_get_hz(clk_sys) != 100000000) {
        set_sys_clock_khz(100000, true);
        debug_reinit();
    }
    if (s_phase == LOGIC_CAPTURE_PHASE_CAPTURING) {
        s_phase = LOGIC_CAPTURE_PHASE_ABORTING;
        capture_stop();
    }
    s_logic_capture_config.total_samples = 0u;
    s_logic_capture_config.rate = 0u;
    s_logic_capture_config.pre_trigger_samples = 0u;
    s_capture_read_offset_bytes = 0u;
    pre_trigger_samples_ = 0u;
    post_trigger_samples_ = 0u;
    pre_trigger_count_ = 0u;
    pre_trigger_first_ = 0;
    trigger_count_ = 0u;
    sm_trigger_mask_ = 0u;
    triggered_channel_ = -1;
    s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
    debug("\nCapture aborted");
}

bool logic_capture_start(const capture_config_t *config, complete_handler_t handler) {
    if (config == NULL || handler == NULL) {
        return false;
    }
    handler_ = handler;
    s_logic_capture_config = *config;
    s_logic_capture_config.channels = LOGIC_CAPTURE_CHANNELS;
    s_capture_read_offset_bytes = 0u;
    pin_base_ = 0;
    uint32_t samples = config->total_samples;
    uint32_t rate = config->rate;
    uint32_t pre_trigger_samples = config->pre_trigger_samples;

    if (samples == 0) return false;
    if (pre_trigger_samples > samples) return false;
    if (pre_trigger_samples > PRE_TRIGGER_BUFFER_SIZE) return false;
    if ((samples - pre_trigger_samples) > POST_TRIGGER_BUFFER_SIZE) return false;
    if (rate == 0) return false;  // if rate must be valid
    configure_inputs();

    s_phase = LOGIC_CAPTURE_PHASE_CAPTURING;
    pre_trigger_samples_ = pre_trigger_samples;
    post_trigger_samples_ = samples - pre_trigger_samples;
    rate_ = rate;

    // Set sys clock
    if (rate > RATE_CHANGE_CLK) {
        if (clock_get_hz(clk_sys) != 200000000) {
            set_sys_clock_khz(200000, true);
            debug_reinit();
        }
        clk_div_ = (float)clock_get_hz(clk_sys) / rate;
    } else {
        if (clock_get_hz(clk_sys) != 100000000) {
            set_sys_clock_khz(100000, true);
            debug_reinit();
        }
        clk_div_ = (float)clock_get_hz(clk_sys) / rate / 32 / 10;
    }
    if (clk_div_ > 0xffff) clk_div_ = 0xffff;

    debug_block("\nSys Clk: %u Clk div (%s): %f", clock_get_hz(clk_sys), rate > RATE_CHANGE_CLK ? "fast" : "slow",
                clk_div_);

    // DMA channel pio0 control: disable pre trigger and enable post trigger
    dma_channel_config config_dma_channel_pio0_ctrl = dma_channel_get_default_config(dma_channel_pio0_ctrl_);
    channel_config_set_transfer_data_size(&config_dma_channel_pio0_ctrl, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_channel_pio0_ctrl, false);
    channel_config_set_read_increment(&config_dma_channel_pio0_ctrl, false);
    channel_config_set_dreq(&config_dma_channel_pio0_ctrl, pio_get_dreq(pio0, sm_mux_, false));
    channel_config_set_chain_to(&config_dma_channel_pio0_ctrl, dma_channel_pio1_ctrl_);
    dma_channel_configure(dma_channel_pio0_ctrl_, &config_dma_channel_pio0_ctrl,
                          &pio0->ctrl,  // write address
                          &pio0_ctrl_,  // read address
                          1, false);

    // DMA channel pio1 control: disable mux and triggers
    dma_channel_config config_dma_channel_pio1_ctrl = dma_channel_get_default_config(dma_channel_pio1_ctrl_);
    channel_config_set_transfer_data_size(&config_dma_channel_pio1_ctrl, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_channel_pio1_ctrl, false);
    channel_config_set_read_increment(&config_dma_channel_pio1_ctrl, false);
    dma_channel_configure(dma_channel_pio1_ctrl_, &config_dma_channel_pio1_ctrl,
                          &pio1->ctrl,  // write address
                          &pio1_ctrl_,  // read address
                          1, false);

    dma_channel_start(dma_channel_pio0_ctrl_);

    // DMA channel pre trigger reload counter
    dma_channel_config config_dma_channel_reload_pre_trigger_counter =
        dma_channel_get_default_config(dma_channel_reload_pre_trigger_counter_);
    channel_config_set_transfer_data_size(&config_dma_channel_reload_pre_trigger_counter, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_channel_reload_pre_trigger_counter, false);
    channel_config_set_read_increment(&config_dma_channel_reload_pre_trigger_counter, false);
    dma_channel_configure(dma_channel_reload_pre_trigger_counter_, &config_dma_channel_reload_pre_trigger_counter,
                          &dma_hw->ch[dma_channel_pre_trigger_].al1_transfer_count_trig,  // write address
                          &reload_counter_,                                               // read address
                          1, false);

    // PIO mux
    offset_mux_ = pio_add_program(pio0, &mux_program);
    pio_config_mux_ = mux_program_get_default_config(offset_mux_);
    sm_config_set_clkdiv(&pio_config_mux_, 1);
    pio_set_irq0_source_enabled(pio0, (enum pio_interrupt_source)(pis_interrupt0), true);
    pio_sm_init(pio0, sm_mux_, offset_mux_, &pio_config_mux_);
    irq_set_exclusive_handler(PIO0_IRQ_0, trigger_handler);
    irq_set_enabled(PIO0_IRQ_0, true);

    // Init pre trigger
    if (rate > RATE_CHANGE_CLK) {
        offset_pre_trigger_ = pio_add_program(pio0, &capture_program);
        pio_config_pre_trigger_ = capture_program_get_default_config(offset_pre_trigger_);
    } else {
        offset_pre_trigger_ = pio_add_program(pio0, &capture_slow_program);
        pio_config_pre_trigger_ = capture_slow_program_get_default_config(offset_pre_trigger_);
    }
    sm_config_set_in_pins(&pio_config_pre_trigger_, pin_base_);
    sm_config_set_in_shift(&pio_config_pre_trigger_, false, true, pin_count_);
    sm_config_set_clkdiv(&pio_config_pre_trigger_, clk_div_);
    pio_sm_init(pio0, sm_pre_trigger_, offset_pre_trigger_, &pio_config_pre_trigger_);
    if (rate > RATE_CHANGE_CLK)
        pio0->instr_mem[offset_pre_trigger_] = pio_encode_in(pio_pins, pin_count_);
    else
        pio0->instr_mem[offset_pre_trigger_] = pio_encode_in(pio_pins, pin_count_) | pio_encode_delay(31);
    dma_channel_config channel_config_pre_trigger = dma_channel_get_default_config(dma_channel_pre_trigger_);
    channel_config_set_transfer_data_size(&channel_config_pre_trigger, DMA_SIZE_16);
    channel_config_set_ring(&channel_config_pre_trigger, true, PRE_TRIGGER_RING_BITS + 1);
    channel_config_set_write_increment(&channel_config_pre_trigger, true);
    channel_config_set_read_increment(&channel_config_pre_trigger, false);
    channel_config_set_dreq(&channel_config_pre_trigger, pio_get_dreq(pio0, sm_pre_trigger_, false));
    channel_config_set_chain_to(&channel_config_pre_trigger, dma_channel_reload_pre_trigger_counter_);
    dma_channel_configure(dma_channel_pre_trigger_, &channel_config_pre_trigger,
                          &pre_trigger_buffer_,         // write address
                          &pio0->rxf[sm_pre_trigger_],  // read address
                          PRE_TRIGGER_RING_TRANSFER_COUNT, true);

    // Init post trigger
    if (rate > RATE_CHANGE_CLK) {
        offset_post_trigger_ = pio_add_program(pio0, &capture_program);
        pio_config_post_trigger_ = capture_program_get_default_config(offset_post_trigger_);
    } else {
        offset_post_trigger_ = pio_add_program(pio0, &capture_slow_program);
        pio_config_post_trigger_ = capture_slow_program_get_default_config(offset_post_trigger_);
    }
    sm_config_set_in_pins(&pio_config_post_trigger_, pin_base_);
    sm_config_set_in_shift(&pio_config_post_trigger_, false, true, pin_count_);
    sm_config_set_clkdiv(&pio_config_post_trigger_, clk_div_);
    pio_sm_init(pio0, sm_post_trigger_, offset_post_trigger_, &pio_config_post_trigger_);
    if (rate > RATE_CHANGE_CLK)
        pio0->instr_mem[offset_post_trigger_] = pio_encode_in(pio_pins, pin_count_);
    else
        pio0->instr_mem[offset_post_trigger_] = pio_encode_in(pio_pins, pin_count_) | pio_encode_delay(31);
    dma_channel_config channel_config_post_trigger = dma_channel_get_default_config(dma_channel_post_trigger_);
    channel_config_set_transfer_data_size(&channel_config_post_trigger, DMA_SIZE_16);
    channel_config_set_write_increment(&channel_config_post_trigger, true);
    channel_config_set_read_increment(&channel_config_post_trigger, false);
    channel_config_set_dreq(&channel_config_post_trigger, pio_get_dreq(pio0, sm_post_trigger_, false));
    dma_channel_set_irq0_enabled(dma_channel_post_trigger_, true);  // raise an interrupt when completed
    irq_set_exclusive_handler(DMA_IRQ_0, capture_complete_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_configure(dma_channel_post_trigger_, &channel_config_post_trigger,
                          &post_trigger_buffer_,         // write address
                          &pio0->rxf[sm_post_trigger_],  // read address
                          post_trigger_samples_, true);

    // Init triggers
    trigger_count_ = 0;
    sm_trigger_mask_ = 0;
    triggered_channel_ = -1;
    for (uint i = 0; i < MAX_TRIGGER_COUNT; i++) {
        if (s_logic_capture_config.trigger[i].is_enabled) {
            if (!set_trigger(s_logic_capture_config.trigger[i])) {
                debug_block("\nFailed to set trigger %u", i);
                capture_stop();
                s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
                return false;
            }
        }
    }

    // Start state machines
    if (!sm_trigger_mask_) {
        pio_sm_set_enabled(pio0, sm_post_trigger_, true);
    } else {
        pio_set_sm_mask_enabled(pio0, (1 << sm_pre_trigger_) | (1 << sm_mux_), true);
        pio_set_sm_mask_enabled(pio1, sm_trigger_mask_, true);
    }

    debug_block("\nCapture start. Samples: %u Rate: %u Pre trigger samples: %u",
                pre_trigger_samples_ + post_trigger_samples_, rate_, pre_trigger_samples_);
    return true;
}

uint16_t logic_capture_get_sample_index(int index) {
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

uint logic_capture_get_samples_count(void) { return pre_trigger_count_ + post_trigger_samples_; }

uint logic_capture_get_pre_trigger_count(void) { return pre_trigger_count_; }

int logic_capture_get_triggered_channel(void) { return triggered_channel_; }

uint logic_capture_get_sm_mux(void) {
    return sm_mux_;
}

uint logic_capture_get_trigger_count(void) {
    return trigger_count_;
}

bool logic_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len) {
    const uint32_t total_bytes = logic_capture_get_samples_count() * sizeof(uint16_t);
    uint32_t remaining_bytes;
    uint16_t chunk_bytes;

    if (block_id == NULL || data == NULL || data_len == NULL) {
        return false;
    }

    if (s_phase != LOGIC_CAPTURE_PHASE_FINALIZED) {
        return false;
    }

    if (s_capture_read_offset_bytes >= total_bytes) {
        return false;
    }

    remaining_bytes = total_bytes - s_capture_read_offset_bytes;
    chunk_bytes = (remaining_bytes > LOGIC_CAPTURE_BLOCK_BYTES) ? LOGIC_CAPTURE_BLOCK_BYTES : (uint16_t)remaining_bytes;

    uint index = s_capture_read_offset_bytes / sizeof(uint16_t);
    for (uint i = 0; i < chunk_bytes / sizeof(uint16_t); ++i) {
        const uint16_t sample = logic_capture_get_sample_index(index);
        data[i * 2u] = (uint8_t)(sample & 0xFFu);
        data[i * 2u + 1u] = (uint8_t)((sample >> 8) & 0xFFu);
        ++index;
    }

    *block_id = (uint16_t)(s_capture_read_offset_bytes / LOGIC_CAPTURE_BLOCK_BYTES);
    *data_len = chunk_bytes;
    s_capture_read_offset_bytes += chunk_bytes;

    return true;
}

capture_state_t logic_capture_get_state(void) {
    if (s_phase == LOGIC_CAPTURE_PHASE_CAPTURING) {
        return CAPTURE_RUNNING;
    }

    return CAPTURE_IDLE;
}

static inline bool set_trigger(trigger_t trigger) {
    if (trigger_count_ < MAX_TRIGGER_COUNT) {
        switch (trigger.match) {
            case TRIGGER_TYPE_LEVEL_HIGH:
                offset_trigger_[trigger_count_] = pio_add_program(pio1, &trigger_level_high_program);
                pio_config_trigger_[trigger_count_] =
                    trigger_level_high_program_get_default_config(offset_trigger_[trigger_count_]);
                break;
            case TRIGGER_TYPE_LEVEL_LOW:
                offset_trigger_[trigger_count_] = pio_add_program(pio1, &trigger_level_low_program);
                pio_config_trigger_[trigger_count_] =
                    trigger_level_low_program_get_default_config(offset_trigger_[trigger_count_]);
                break;
            case TRIGGER_TYPE_EDGE_HIGH:
                offset_trigger_[trigger_count_] = pio_add_program(pio1, &trigger_edge_high_program);
                pio_config_trigger_[trigger_count_] =
                    trigger_edge_high_program_get_default_config(offset_trigger_[trigger_count_]);
                break;
            case TRIGGER_TYPE_EDGE_LOW:
                offset_trigger_[trigger_count_] = pio_add_program(pio1, &trigger_edge_low_program);
                pio_config_trigger_[trigger_count_] =
                    trigger_edge_low_program_get_default_config(offset_trigger_[trigger_count_]);
                break;
        }
        sm_config_set_clkdiv(&pio_config_trigger_[trigger_count_], clk_div_);
        sm_config_set_in_pins(&pio_config_trigger_[trigger_count_], trigger.pin);
        pio_sm_init(pio1, sm_trigger_[trigger_count_], offset_trigger_[trigger_count_],
                    &pio_config_trigger_[trigger_count_]);
        sm_trigger_mask_ |= 1 << sm_trigger_[trigger_count_];

        dma_channel_config channel_config_trigger =
            dma_channel_get_default_config(dma_channel_trigger_[trigger_count_]);
        channel_config_set_transfer_data_size(&channel_config_trigger, DMA_SIZE_32);
        channel_config_set_write_increment(&channel_config_trigger, false);
        channel_config_set_read_increment(&channel_config_trigger, false);
        channel_config_set_dreq(&channel_config_trigger, pio_get_dreq(pio1, sm_trigger_[trigger_count_], false));
        dma_channel_configure(dma_channel_trigger_[trigger_count_], &channel_config_trigger,
                              &pio0->txf[sm_mux_],                        // write address
                              &triggered_channel_index_[trigger_count_],  // read address
                              1, true);

        if (debug_is_enabled()) {
            char match[15] = "";
            switch (s_logic_capture_config.trigger[trigger_count_].match) {
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
            debug_block("\n-Set trigger %u Pin: %u Match: %s", trigger_count_,
                        s_logic_capture_config.trigger[trigger_count_].pin, match);
        }

        trigger_count_++;
        return true;
    }
    return false;
}

static inline void trigger_handler(void) {
    triggered_channel_ = pio_sm_get(pio0, sm_mux_);
    pio_interrupt_clear(pio0, 0);
}

static inline void capture_complete_handler(void) {
    dma_hw->ints0 = 1u << dma_channel_post_trigger_;
    if (s_phase == LOGIC_CAPTURE_PHASE_CAPTURING) {
        // Set pre trigger range
        pre_trigger_first_ = 0;
        pre_trigger_count_ = 0;
        if (pre_trigger_samples_) {
            uint transfer_count = PRE_TRIGGER_RING_TRANSFER_COUNT - dma_hw->ch[dma_channel_pre_trigger_].transfer_count;
            pre_trigger_first_ = (int)(transfer_count % PRE_TRIGGER_BUFFER_SIZE) - (int)pre_trigger_samples_;
            pre_trigger_count_ = pre_trigger_samples_;
            if ((pre_trigger_first_ < 0) && (transfer_count < PRE_TRIGGER_BUFFER_SIZE)) {
                pre_trigger_first_ = 0;
                pre_trigger_count_ = transfer_count;
            }
        }
        capture_stop();
        s_phase = LOGIC_CAPTURE_PHASE_FINALIZED;
        if (handler_) handler_();
    } else {
        s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
    }
}

static inline void capture_stop(void) {
    pio_set_sm_mask_enabled(pio0, (1 << sm_mux_) | (1 << sm_pre_trigger_) | (1 << sm_post_trigger_), false);
    pio_set_sm_mask_enabled(pio1, sm_trigger_mask_, false);
    dma_channel_abort(dma_channel_pre_trigger_);
    dma_channel_abort(dma_channel_post_trigger_);
    dma_channel_abort(dma_channel_pio0_ctrl_);
    dma_channel_abort(dma_channel_pio1_ctrl_);
    for (uint i = 0; i < trigger_count_; i++) {
        dma_channel_abort(dma_channel_trigger_[i]);
        pio_sm_clear_fifos(pio1, sm_trigger_[i]);
    }
    pio_sm_clear_fifos(pio0, sm_mux_);
    pio_sm_clear_fifos(pio0, sm_pre_trigger_);
    pio_sm_clear_fifos(pio0, sm_post_trigger_);
    pio_clear_instruction_memory(pio0);
    pio_clear_instruction_memory(pio1);
}

static void configure_inputs(void) {
    for (uint32_t pin = 0u; pin < LOGIC_CAPTURE_CHANNELS; ++pin) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_pull_down(pin);
    }
}
