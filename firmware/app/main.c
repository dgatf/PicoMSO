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

/*
 * PicoMSO USB control-plane + data-plane entry point (example).
 *
 * Demonstrates the minimal wiring between:
 *   1. USB transport backend  (firmware/transport/usb/)
 *   2. Integration layer      (firmware/integration/)
 *   3. Protocol dispatch      (firmware/protocol/)
 *   4. Capture controller     (firmware/common/)
 *
 * Control-plane commands handled: GET_INFO, GET_CAPABILITIES,
 * GET_STATUS, SET_MODE, REQUEST_CAPTURE, READ_DATA_BLOCK.
 *
 * One-shot capture flow handled here:
 *   1. The host selects logic or oscilloscope mode with SET_MODE().
 *   2. The host sends REQUEST_CAPTURE with the requested total sample count.
 *   3. The device performs the full one-shot capture for the selected mode.
 *   4. Only after capture completion does READ_DATA_BLOCK return fixed-size
 *      chunks from the finalized stored capture over the EP6 IN bulk endpoint.
 *
 * Out of scope for this example:
 *   - SUMP protocol
 *   - Live data streaming during acquisition
 *   - A separate oscilloscope-specific trigger algorithm
 *   - Shared logic/scope abstractions beyond the current one-shot paths
 *
 * Wire path (control-plane commands):
 *   Host (vendor OUT control transfer on EP0)
 *     → usb_transport_iface.receive()
 *     → integration_process_one()
 *     → picomso_dispatch()
 *     → per-command handler (reads/writes capture_controller_t)
 *     → usb_transport_iface.send()  (EP6 IN bulk transfer)
 *   → Host
 *
 * Wire path (REQUEST_CAPTURE + READ_DATA_BLOCK):
 *   Host (vendor OUT control transfer on EP0, REQUEST_CAPTURE = 0x05)
 *     → usb_transport_iface.receive()
 *     → integration_process_one()
 *     → picomso_dispatch()
 *     → picomso_handle_request_capture()
 *       → logic_capture_start()
 *         → request-defined full capture length
 *         → circular pre-trigger buffering
 *         → trigger detect on GPIO 0
 *         → post-trigger completion
 *         → finalized stored capture buffer
 *   Host (later vendor OUT control transfer on EP0, READ_DATA_BLOCK = 0x06)
 *     → usb_transport_iface.receive()
 *     → integration_process_one()
 *     → picomso_dispatch()
 *     → picomso_handle_read_data_block()
 *       → logic_capture_read_block()
 *         → next fixed-size chunk from finalized capture
 *     → usb_transport_iface.send()  (EP6 IN bulk transfer, DATA_BLOCK = 0x82)
 *   → Host
 */

#include "pico/stdlib.h"

#include "usb_transport.h"
#include "integration.h"
#include "debug.h"
#include "types.h"
#include "hardware/clocks.h"

char debug_message_[DEBUG_BUFFER_SIZE];
bool debug_ = false;

void set_pin_config(void);

int main(void)
{
    if (clock_get_hz(clk_sys) != 100000000) set_sys_clock_khz(100000, true);
    stdio_init_all();
    set_pin_config();
    debug_init(115200, &debug_message_[0], &debug_);
    debug("\nPicoMSO firmware starting...");
    
    /*
     * Step 1: Initialise the USB hardware.
     *
     * usb_transport_init() is a thin wrapper around usb_device_init().
     * It configures the RP2040 USB controller, registers endpoint
     * descriptors, and arms EP0 for control transfers.
     * The protocol layer is NOT touched here; it is initialised lazily
     * on the first picomso_dispatch() call.
     */
    usb_transport_init();

    /*
     * Step 2: Bind the USB transport interface to a transport context.
     *
     * usb_transport_iface is a const transport_interface_t that maps
     *   is_ready → usb_is_configured()
     *   send     → EP6 IN bulk transfer
     *   receive  → EP0 OUT vendor control transfer (polled)
     *
     * user_data is NULL; the USB backend uses static module-level state.
     */
    transport_ctx_t transport;
    transport_init(&transport, &usb_transport_iface, NULL);

    /*
     * Step 3: Bind the transport context to the integration layer.
     *
     * integration_init() records the transport pointer; no I/O occurs.
     */
    integration_ctx_t integration;
    integration_init(&integration, &transport);

    /*
     * Step 4: Control-plane and data-plane polling loop.
     *
     * integration_process_one() performs one receive → dispatch → send
     * cycle.  When no data has arrived on EP0 it returns INTEGRATION_OK
     * immediately (the USB receive callback returns zero bytes).  The
     * loop therefore busy-polls at near-maximum RP2040 speed; a real
     * application would add sleep_ms() or use interrupt-driven signalling
     * to yield the CPU while idle.
     *
     * REQUEST_CAPTURE performs the full one-shot acquisition synchronously.
     * READ_DATA_BLOCK then serves the completed capture buffer in fixed-size
     * chunks over EP6 IN bulk without exposing live acquisition data.
     */
    while (true) {
        integration_process_one(&integration);
    }

    /* Unreachable. */
    return 0;
}

void set_pin_config(void) {
    /*
     *   Connect GPIO to GND at boot to select/enable:
     *   - GPIO 18: debug mode on. Output is on GPIO 16 at 115200bps.
     *
     *   Defaults (option not grounded):
     *   - Debug disabled
     */

    // configure pins
    gpio_init_mask((1 << GPIO_DEBUG_ENABLE));
    gpio_set_dir_masked((1 << GPIO_DEBUG_ENABLE), false);
    gpio_pull_up(GPIO_DEBUG_ENABLE);

    // set default config
    debug_ = false;

    // read pin config
    if (!gpio_get(GPIO_DEBUG_ENABLE)) debug_ = true;
}