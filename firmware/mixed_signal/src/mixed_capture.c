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
