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
#include "capture_controller.h"
#include "debug.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#define LOGIC_CAPTURE_CHANNELS 16u
#define LOGIC_CAPTURE_TRIGGER_TIMEOUT_SPINS 4096u

#define PRE_TRIGGER_RING_BITS 12
#define PRE_TRIGGER_BUFFER_SIZE (1 << PRE_TRIGGER_RING_BITS)
#define POST_TRIGGER_BUFFER_SIZE 50000
#define PRE_TRIGGER_RING_TRANSFER_COUNT ((0xffffffffu / PRE_TRIGGER_BUFFER_SIZE) * PRE_TRIGGER_BUFFER_SIZE)
#define LOGIC_CAPTURE_MAX_TRIGGER_COUNT 2u
#define RATE_CHANGE_CLK 200000u

typedef enum logic_capture_phase_t {
    LOGIC_CAPTURE_PHASE_DISARMED = 0,
    LOGIC_CAPTURE_PHASE_ARMED,
    LOGIC_CAPTURE_PHASE_CAPTURING,
    LOGIC_CAPTURE_PHASE_ABORTING,
    LOGIC_CAPTURE_PHASE_FINALIZED
} logic_capture_phase_t;

static capture_config_t s_logic_capture_config = {.total_samples = 0u,
                                                  .rate = 0u,
                                                  .pre_trigger_samples = 0u,
                                                  .channels = LOGIC_CAPTURE_CHANNELS,
                                                  .trigger = {
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                      {.is_enabled = false, .pin = 0u, .match = TRIGGER_TYPE_LEVEL_LOW},
                                                  }};

static volatile logic_capture_phase_t s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
static uint32_t s_capture_read_offset_bytes = 0u;
static bool s_activation_armed = false;

static const uint sm_pre_trigger_ = 0, sm_post_trigger_ = 1, sm_mux_ = 3, dma_pre_trigger_capture_ = 0,
                  dma_post_trigger_capture_ = 1, dma_arm_post_trigger_ = 2, dma_stop_trigger_path_ = 3,
                  dma_pre_trigger_reload_ = 4, dma_trigger_to_mux_[LOGIC_CAPTURE_MAX_TRIGGER_COUNT] = {5, 6},
                  sm_trigger_[LOGIC_CAPTURE_MAX_TRIGGER_COUNT] = {0, 1},
                  reload_counter_ = PRE_TRIGGER_RING_TRANSFER_COUNT;

static uint offset_pre_trigger_, offset_post_trigger_, pre_trigger_samples_, post_trigger_samples_,
    pin_count_ = LOGIC_CAPTURE_CHANNELS, trigger_count_, sm_trigger_mask_, trigger_mask_, pin_base_, rate_, offset_mux_,
    offset_trigger_[LOGIC_CAPTURE_MAX_TRIGGER_COUNT];
static volatile uint pre_trigger_count_;
static volatile int pre_trigger_first_, triggered_channel_;
static float clk_div_;
static volatile uint pio0_ctrl_ = (1 << sm_post_trigger_), pio1_ctrl_ = 0;
static uint16_t pre_trigger_buffer_[PRE_TRIGGER_BUFFER_SIZE]
    __attribute__((aligned(PRE_TRIGGER_BUFFER_SIZE * sizeof(uint16_t)))),
    post_trigger_buffer_[POST_TRIGGER_BUFFER_SIZE];
static pio_sm_config pio_config_trigger_[LOGIC_CAPTURE_MAX_TRIGGER_COUNT], pio_config_pre_trigger_,
    pio_config_post_trigger_, pio_config_mux_;
static const uint triggered_channel_index_[LOGIC_CAPTURE_MAX_TRIGGER_COUNT] = {0, 1};

static void (*handler_)(void) = NULL;

static void configure_inputs(void);
static inline void trigger_handler(void);
static inline bool set_trigger(trigger_t trigger);
static inline void capture_complete_handler(void);
static inline void capture_stop(void);

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

void logic_capture_reset(void) {
    debug("\n[logic] reset begin phase=%s total_samples=%lu read_offset=%lu", logic_capture_phase_name(s_phase),
          (unsigned long)s_logic_capture_config.total_samples, (unsigned long)s_capture_read_offset_bytes);
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

    handler_ = handler;
    s_logic_capture_config = *config;
    s_logic_capture_config.channels = LOGIC_CAPTURE_CHANNELS;
    s_capture_read_offset_bytes = 0u;
    pin_base_ = 0;

    uint32_t samples = config->total_samples;
    uint32_t rate = config->rate;
    uint32_t pre_trigger_samples = config->pre_trigger_samples;

    debug("\n[logic] prepare request phase=%s samples=%lu rate=%lu pre=%lu", logic_capture_phase_name(s_phase),
          (unsigned long)samples, (unsigned long)rate, (unsigned long)pre_trigger_samples);

    if (samples == 0) {
        debug("\n[logic] prepare rejected reason=zero_samples");
        return false;
    }
    if (pre_trigger_samples > samples) {
        debug("\n[logic] prepare rejected reason=pre_gt_total pre=%lu samples=%lu", (unsigned long)pre_trigger_samples,
              (unsigned long)samples);
        return false;
    }
    if (pre_trigger_samples > PRE_TRIGGER_BUFFER_SIZE) {
        debug("\n[logic] prepare rejected reason=pre_buffer_overflow pre=%lu max=%u",
              (unsigned long)pre_trigger_samples, PRE_TRIGGER_BUFFER_SIZE);
        return false;
    }
    if ((samples - pre_trigger_samples) > POST_TRIGGER_BUFFER_SIZE) {
        debug("\n[logic] prepare rejected reason=post_buffer_overflow post=%lu max=%u",
              (unsigned long)(samples - pre_trigger_samples), POST_TRIGGER_BUFFER_SIZE);
        return false;
    }
    if (rate == 0) {
        debug("\n[logic] prepare rejected reason=zero_rate");
        return false;
    }

    configure_inputs();

    pre_trigger_samples_ = pre_trigger_samples;
    post_trigger_samples_ = samples - pre_trigger_samples;
    rate_ = rate;
    pre_trigger_count_ = 0u;
    pre_trigger_first_ = 0;
    trigger_count_ = 0u;
    sm_trigger_mask_ = 0u;
    triggered_channel_ = -1;
    s_activation_armed = false;

    if (rate > RATE_CHANGE_CLK) {
        if (clock_get_hz(clk_sys) != 200000000) {
            set_sys_clock_khz(200000, true);
            debug_reinit();
        }
    } else {
        if (clock_get_hz(clk_sys) != 100000000) {
            set_sys_clock_khz(100000, true);
            debug_reinit();
        }
    }

    clk_div_ = (float)clock_get_hz(clk_sys) / rate;
    if (clk_div_ > 0xffff) clk_div_ = 0xffff;

    debug_block("\n[logic] prepare clocks sys_clk=%u clk_div=%f", clock_get_hz(clk_sys), clk_div_);

    dma_channel_config config_dma_arm_post_trigger = dma_channel_get_default_config(dma_arm_post_trigger_);
    channel_config_set_transfer_data_size(&config_dma_arm_post_trigger, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_arm_post_trigger, false);
    channel_config_set_read_increment(&config_dma_arm_post_trigger, false);
    channel_config_set_dreq(&config_dma_arm_post_trigger, pio_get_dreq(pio0, sm_mux_, false));
    channel_config_set_chain_to(&config_dma_arm_post_trigger, dma_stop_trigger_path_);
    dma_channel_configure(dma_arm_post_trigger_, &config_dma_arm_post_trigger, &pio0->ctrl, &pio0_ctrl_, 1, false);

    dma_channel_config config_dma_stop_trigger_path = dma_channel_get_default_config(dma_stop_trigger_path_);
    channel_config_set_transfer_data_size(&config_dma_stop_trigger_path, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_stop_trigger_path, false);
    channel_config_set_read_increment(&config_dma_stop_trigger_path, false);
    dma_channel_configure(dma_stop_trigger_path_, &config_dma_stop_trigger_path, &pio1->ctrl, &pio1_ctrl_, 1, false);

    dma_channel_config config_dma_pre_trigger_reload = dma_channel_get_default_config(dma_pre_trigger_reload_);
    channel_config_set_transfer_data_size(&config_dma_pre_trigger_reload, DMA_SIZE_32);
    channel_config_set_write_increment(&config_dma_pre_trigger_reload, false);
    channel_config_set_read_increment(&config_dma_pre_trigger_reload, false);
    dma_channel_configure(dma_pre_trigger_reload_, &config_dma_pre_trigger_reload,
                          &dma_hw->ch[dma_pre_trigger_capture_].al1_transfer_count_trig, &reload_counter_, 1, false);

    offset_mux_ = pio_add_program(pio0, &mux_program);
    pio_config_mux_ = mux_program_get_default_config(offset_mux_);
    sm_config_set_clkdiv(&pio_config_mux_, 1);
    pio_set_irq0_source_enabled(pio0, (enum pio_interrupt_source)(pis_interrupt0), true);
    pio_sm_init(pio0, sm_mux_, offset_mux_, &pio_config_mux_);
    irq_set_exclusive_handler(PIO0_IRQ_0, trigger_handler);
    irq_set_enabled(PIO0_IRQ_0, true);

    offset_pre_trigger_ = pio_add_program(pio0, &capture_program);
    pio_config_pre_trigger_ = capture_program_get_default_config(offset_pre_trigger_);

    sm_config_set_in_pins(&pio_config_pre_trigger_, pin_base_);
    sm_config_set_in_shift(&pio_config_pre_trigger_, false, true, pin_count_);
    sm_config_set_clkdiv(&pio_config_pre_trigger_, clk_div_);
    pio_sm_init(pio0, sm_pre_trigger_, offset_pre_trigger_, &pio_config_pre_trigger_);

    pio0->instr_mem[offset_pre_trigger_] = pio_encode_in(pio_pins, pin_count_);

    dma_channel_config channel_config_pre_trigger_capture = dma_channel_get_default_config(dma_pre_trigger_capture_);
    channel_config_set_transfer_data_size(&channel_config_pre_trigger_capture, DMA_SIZE_16);
    channel_config_set_ring(&channel_config_pre_trigger_capture, true, PRE_TRIGGER_RING_BITS + 1);
    channel_config_set_write_increment(&channel_config_pre_trigger_capture, true);
    channel_config_set_read_increment(&channel_config_pre_trigger_capture, false);
    channel_config_set_dreq(&channel_config_pre_trigger_capture, pio_get_dreq(pio0, sm_pre_trigger_, false));
    channel_config_set_chain_to(&channel_config_pre_trigger_capture, dma_pre_trigger_reload_);
    // Armed explicitly in logic_capture_arm() so mixed mode can keep both
    // backends fully prepared before the final activation writes.
    dma_channel_configure(dma_pre_trigger_capture_, &channel_config_pre_trigger_capture, &pre_trigger_buffer_,
                          &pio0->rxf[sm_pre_trigger_], PRE_TRIGGER_RING_TRANSFER_COUNT, false);

    offset_post_trigger_ = pio_add_program(pio0, &capture_program);
    pio_config_post_trigger_ = capture_program_get_default_config(offset_post_trigger_);

    sm_config_set_in_pins(&pio_config_post_trigger_, pin_base_);
    sm_config_set_in_shift(&pio_config_post_trigger_, false, true, pin_count_);
    sm_config_set_clkdiv(&pio_config_post_trigger_, clk_div_);
    pio_sm_init(pio0, sm_post_trigger_, offset_post_trigger_, &pio_config_post_trigger_);

    pio0->instr_mem[offset_post_trigger_] = pio_encode_in(pio_pins, pin_count_);

    dma_channel_config channel_config_post_trigger_capture = dma_channel_get_default_config(dma_post_trigger_capture_);
    channel_config_set_transfer_data_size(&channel_config_post_trigger_capture, DMA_SIZE_16);
    channel_config_set_write_increment(&channel_config_post_trigger_capture, true);
    channel_config_set_read_increment(&channel_config_post_trigger_capture, false);
    channel_config_set_dreq(&channel_config_post_trigger_capture, pio_get_dreq(pio0, sm_post_trigger_, false));
    dma_channel_set_irq0_enabled(dma_post_trigger_capture_, true);
    irq_set_exclusive_handler(DMA_IRQ_0, capture_complete_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_configure(dma_post_trigger_capture_, &channel_config_post_trigger_capture, &post_trigger_buffer_,
                          &pio0->rxf[sm_post_trigger_], post_trigger_samples_, false);

    for (uint i = 0; i < LOGIC_CAPTURE_MAX_TRIGGER_COUNT; i++) {
        if (s_logic_capture_config.trigger[i].is_enabled) {
            if (!set_trigger(s_logic_capture_config.trigger[i])) {
                debug_block("\n[logic] prepare rejected reason=trigger_setup_failed index=%u", i);
                capture_stop();
                s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
                return false;
            }
        }
    }

    trigger_gate->enabled = trigger_count_ > 0u;
    trigger_gate->dreq = trigger_gate->enabled ? pio_get_dreq(pio0, sm_mux_, false) : 0u;
    activation->pio0_enable_mask = trigger_gate->enabled ? ((1u << sm_pre_trigger_) | (1u << sm_mux_))
                                                         : (1u << sm_post_trigger_);
    activation->pio1_enable_mask = trigger_gate->enabled ? sm_trigger_mask_ : 0u;

    s_phase = LOGIC_CAPTURE_PHASE_ARMED;

    debug_block("\n[logic] prepare armed phase=%s samples=%u rate=%u pre=%u post=%u triggers=%u",
                logic_capture_phase_name(s_phase), pre_trigger_samples_ + post_trigger_samples_, rate_,
                pre_trigger_samples_, post_trigger_samples_, trigger_count_);

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

    dma_channel_start(dma_post_trigger_capture_);
    if (sm_trigger_mask_) {
        dma_channel_start(dma_arm_post_trigger_);
        dma_channel_start(dma_pre_trigger_capture_);
        for (uint i = 0; i < trigger_count_; ++i) {
            dma_channel_start(dma_trigger_to_mux_[i]);
        }
    }

    s_activation_armed = true;

    debug_block("\n[logic] arm ready phase=%s samples=%u rate=%u pre=%u post=%u triggers=%u",
                logic_capture_phase_name(s_phase), pre_trigger_samples_ + post_trigger_samples_, rate_,
                pre_trigger_samples_, post_trigger_samples_, trigger_count_);
    return true;
}

void logic_capture_mark_capturing(void) {
    if (s_phase != LOGIC_CAPTURE_PHASE_ARMED) return;

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
    if (activation->pio1_enable_mask) pio1->ctrl = activation->pio1_enable_mask;

    logic_capture_mark_capturing();

    debug_block("\n[logic] activate started phase=%s samples=%u rate=%u pre=%u post=%u triggers=%u",
                logic_capture_phase_name(s_phase), pre_trigger_samples_ + post_trigger_samples_, rate_,
                pre_trigger_samples_, post_trigger_samples_, trigger_count_);
}

bool logic_capture_start(const capture_config_t *config, complete_handler_t handler) {
    capture_trigger_gate_t trigger_gate = {.enabled = false, .dreq = 0u};
    logic_capture_activation_t activation = {.pio0_enable_mask = 0u, .pio1_enable_mask = 0u};

    if (!logic_capture_prepare(config, handler, &trigger_gate, &activation)) return false;
    if (!logic_capture_arm()) {
        logic_capture_reset();
        return false;
    }

    logic_capture_activate(&activation);
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

bool logic_capture_read_block(uint16_t *block_id, uint8_t *data, uint16_t *data_len) {
    const uint32_t total_bytes = logic_capture_get_samples_count() * sizeof(uint16_t);
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

static inline bool set_trigger(trigger_t trigger) {
    if (trigger_count_ < LOGIC_CAPTURE_MAX_TRIGGER_COUNT) {
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

        dma_channel_config channel_config_trigger_to_mux =
            dma_channel_get_default_config(dma_trigger_to_mux_[trigger_count_]);
        channel_config_set_transfer_data_size(&channel_config_trigger_to_mux, DMA_SIZE_32);
        channel_config_set_write_increment(&channel_config_trigger_to_mux, false);
        channel_config_set_read_increment(&channel_config_trigger_to_mux, false);
        channel_config_set_dreq(&channel_config_trigger_to_mux, pio_get_dreq(pio1, sm_trigger_[trigger_count_], false));
        // Armed explicitly in logic_capture_arm() so trigger routing is waiting
        // before mixed activation enables the source state machines.
        dma_channel_configure(dma_trigger_to_mux_[trigger_count_], &channel_config_trigger_to_mux, &pio0->txf[sm_mux_],
                              &triggered_channel_index_[trigger_count_], 1, false);

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
            debug_block("\n[logic] trigger configured index=%u pin=%u match=%s", trigger_count_,
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
    debug("\n[logic] trigger fired channel=%d", triggered_channel_);
}

static inline void capture_complete_handler(void) {
    dma_hw->ints0 = 1u << dma_post_trigger_capture_;
    if (s_phase == LOGIC_CAPTURE_PHASE_CAPTURING) {
        pre_trigger_first_ = 0;
        pre_trigger_count_ = 0;
        if (pre_trigger_samples_) {
            uint transfer_count = PRE_TRIGGER_RING_TRANSFER_COUNT - dma_hw->ch[dma_pre_trigger_capture_].transfer_count;
            pre_trigger_first_ = (int)(transfer_count % PRE_TRIGGER_BUFFER_SIZE) - (int)pre_trigger_samples_;
            pre_trigger_count_ = pre_trigger_samples_;
            if ((pre_trigger_first_ < 0) && (transfer_count < PRE_TRIGGER_BUFFER_SIZE)) {
                pre_trigger_first_ = 0;
                pre_trigger_count_ = transfer_count;
            }
        }
        capture_stop();
        s_phase = LOGIC_CAPTURE_PHASE_FINALIZED;
        debug(
            "\n[logic] complete phase=%s triggered_channel=%d pre_count=%u post_count=%u total_samples=%u "
            "pending_bytes=%lu",
            logic_capture_phase_name(s_phase), triggered_channel_, pre_trigger_count_, post_trigger_samples_,
            pre_trigger_count_ + post_trigger_samples_,
            (unsigned long)(logic_capture_get_samples_count() * sizeof(uint16_t)));
        if (handler_) handler_();
    } else {
        s_phase = LOGIC_CAPTURE_PHASE_DISARMED;
        debug("\n[logic] complete ignored phase=%s", logic_capture_phase_name(s_phase));
    }
}

static inline void capture_stop(void) {
    pio_set_sm_mask_enabled(pio0, (1 << sm_mux_) | (1 << sm_pre_trigger_) | (1 << sm_post_trigger_), false);
    pio_set_sm_mask_enabled(pio1, sm_trigger_mask_, false);
    dma_channel_abort(dma_pre_trigger_capture_);
    dma_channel_abort(dma_post_trigger_capture_);
    dma_channel_abort(dma_arm_post_trigger_);
    dma_channel_abort(dma_stop_trigger_path_);
    for (uint i = 0; i < trigger_count_; i++) {
        dma_channel_abort(dma_trigger_to_mux_[i]);
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
