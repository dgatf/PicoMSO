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
 * GET_STATUS, SET_MODE.
 *
 * Data-plane command handled: READ_DATA_BLOCK.
 *   The host sends READ_DATA_BLOCK as a vendor OUT control transfer on EP0.
 *   In logic mode the device performs a one-shot logic capture on GPIO 0..15:
 *   the capture is armed by SET_MODE(LOGIC), pre-trigger samples are retained
 *   in a circular buffer, a rising edge on GPIO 0 finalizes the trigger point,
 *   and the finalized 64-byte block is returned over the EP6 IN bulk endpoint.
 *
 * Out of scope for this example:
 *   - Oscilloscope capture
 *   - Shared logic/scope abstractions beyond this logic path
 *   - SUMP protocol
 *   - Streaming beyond one finalized capture block
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
 * Wire path (READ_DATA_BLOCK):
 *   Host (vendor OUT control transfer on EP0, READ_DATA_BLOCK = 0x05)
 *     → usb_transport_iface.receive()
 *     → integration_process_one()
 *     → picomso_dispatch()
 *     → picomso_handle_read_data_block()
 *       → logic_capture_read_block()
 *         → arm-state pre-trigger ring
 *         → trigger detect on GPIO 0
 *         → finite post-trigger sampling
 *         → finalized 64-byte one-shot block
 *     → usb_transport_iface.send()  (EP6 IN bulk transfer, DATA_BLOCK = 0x82)
 *   → Host
 */

#include "pico/stdlib.h"

#include "usb_transport.h"
#include "integration.h"

int main(void)
{
    stdio_init_all();

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
     * READ_DATA_BLOCK is handled transparently: in logic mode the protocol
     * layer asks the logic capture backend to finalize one armed capture,
     * then sends the resulting 64-byte sample block over EP6 IN bulk.
     */
    while (true) {
        integration_process_one(&integration);
    }

    /* Unreachable. */
    return 0;
}
