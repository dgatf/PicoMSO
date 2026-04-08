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

 #include "mixed_capture.h"

#include "hardware/adc.h"
#include "hardware/pio.h"

#include "logic_capture.h"
#include "scope_capture.h"

/*
 * No spare DMA channels are available to build a tighter synchronized mixed-mode
 * start chain, so this explicit software activation point is the intended
 * best-effort sync boundary.
 */
static void mixed_capture_activate(const logic_capture_activation_t *logic_activation) {
    if (logic_activation == NULL) return;

    adc_run(true);
    pio0->ctrl = logic_activation->pio0_enable_mask;
    if (logic_activation->pio1_enable_mask) pio1->ctrl = logic_activation->pio1_enable_mask;

    scope_capture_mark_capturing();
    logic_capture_mark_capturing();
}

bool mixed_capture_start(const capture_config_t *logic_config, const capture_config_t *scope_config,
                         complete_handler_t logic_handler, complete_handler_t scope_handler) {
    capture_trigger_gate_t trigger_gate = {.enabled = false, .dreq = 0u};
    logic_capture_activation_t logic_activation = {.pio0_enable_mask = 0u, .pio1_enable_mask = 0u};

    if (logic_config == NULL || scope_config == NULL || logic_handler == NULL || scope_handler == NULL) {
        return false;
    }

    if (!logic_capture_prepare(logic_config, logic_handler, &trigger_gate, &logic_activation)) {
        return false;
    }

    if (!scope_capture_prepare(scope_config, scope_handler, &trigger_gate)) {
        logic_capture_reset();
        return false;
    }

    if (!logic_capture_arm()) {
        mixed_capture_reset();
        return false;
    }

    if (!scope_capture_arm()) {
        mixed_capture_reset();
        return false;
    }

    mixed_capture_activate(&logic_activation);

    return true;
}

void mixed_capture_reset(void) {
    logic_capture_reset();
    scope_capture_reset();
}
