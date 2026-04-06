/*
 * PicoMSO - Mixed Signal Oscilloscope
 * Copyright (C) 2026 Daniel Gorbea
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

/*
 * PicoMSO USB transport backend – implementation (Phase 0).
 *
 * Glue layer connecting the custom RP2040 USB driver (usb.c / usb.h) to
 * the generic transport_interface_t abstraction.
 *
 * Receive path
 * ~~~~~~~~~~~~
 * The USB driver calls control_transfer_handler() (provided here) for every
 * EP0 control transfer event.  When the host sends a vendor OUT control
 * transfer, the data bytes arrive in the STAGE_DATA callback.  This handler
 * copies them into a static receive buffer and raises a flag.
 * transport_receive() drains the buffer on the next poll; it returns zero
 * bytes (TRANSPORT_OK) while the flag is clear so callers can poll safely.
 *
 * Send path
 * ~~~~~~~~~
 * transport_send() binds the caller's buffer to the EP6_IN endpoint's
 * data_buffer pointer and calls usb_init_transfer().  The USB driver copies
 * from data_buffer into DPRAM automatically.  A tight loop blocks until
 * ep->is_completed is set (suitable for small control-plane packets).
 *
 * Readiness
 * ~~~~~~~~~
 * transport_is_ready() returns usb_is_configured().
 *
 * Constraints
 * ~~~~~~~~~~~
 *   - No new buffering scheme beyond a single static packet buffer.
 *   - No new state machine; USB state management stays inside usb.c.
 *   - No TinyUSB dependency.
 *   - usb.c, usb.h, usb_common.h, usb_config.c, and usb_config.h are
 *     used as-is without modification.
 */

#include "usb_transport.h"

#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "usb.h"

/* Maximum bytes for a single incoming control packet:
 * 8-byte protocol header + up to 512 bytes of payload. */
#define USB_TRANSPORT_RX_BUF_SIZE  520u

/* Timeout (microseconds) for a blocking EP6_IN bulk send.
 * 100 ms is generous for a small control-plane packet at USB full speed. */
#define USB_TRANSPORT_SEND_TIMEOUT_US  100000u

/* Single-packet receive buffer populated by control_transfer_handler(). */
static uint8_t rx_buf[USB_TRANSPORT_RX_BUF_SIZE];
static size_t  rx_len        = 0;
static bool    rx_ready      = false;
static bool    rx_truncated  = false;  /* set when an oversized packet arrived */

/* -----------------------------------------------------------------------
 * control_transfer_handler
 *
 * The application layer that links against picomso_usb_transport must
 * provide this symbol.  This glue file satisfies that requirement so that
 * the integration layer never needs to see any USB header.
 *
 * Only OUT transfers (host → device) at STAGE_DATA are captured; all
 * other stages and IN transfers are silently ignored.
 * ----------------------------------------------------------------------- */

void control_transfer_handler(uint8_t *buf,
                               volatile struct usb_setup_packet *pkt,
                               uint8_t stage)
{
    if (stage == STAGE_DATA && !(pkt->bmRequestType & USB_DIR_IN)) {
        size_t len = pkt->wLength;
        rx_truncated = (len > USB_TRANSPORT_RX_BUF_SIZE);
        if (rx_truncated) {
            len = USB_TRANSPORT_RX_BUF_SIZE;
        }
        memcpy(rx_buf, buf, len);
        rx_len   = len;
        rx_ready = true;
    }
}

/* -----------------------------------------------------------------------
 * Backend callbacks
 * ----------------------------------------------------------------------- */

static bool usb_transport_is_ready_cb(void *user_data)
{
    (void)user_data;
    return usb_is_configured();
}

static transport_result_t usb_transport_send_cb(void *user_data,
                                                  const uint8_t *buf,
                                                  size_t len)
{
    (void)user_data;

    if (!usb_is_configured()) {
        return TRANSPORT_ERR_NOT_READY;
    }

    struct usb_endpoint_configuration *ep =
        usb_get_endpoint_configuration(EP6_IN_ADDR);
    if (ep == NULL) {
        return TRANSPORT_ERR_IO;
    }

    /* Bind caller's buffer so the USB driver copies it into DPRAM.
     * usb_endpoint_configuration.data_buffer is uint8_t * (not const);
     * we cannot change that struct definition (it lives in usb_config.h,
     * used as-is).  The driver only reads from data_buffer when the
     * endpoint is IN (TX), so the cast is safe. */
    ep->data_buffer      = (uint8_t *)(uintptr_t)buf;
    ep->data_buffer_size = len;

    usb_init_transfer(ep, (int32_t)len);

    /* Block until the bulk transfer completes or timeout elapses. */
    absolute_time_t deadline = make_timeout_time_us(USB_TRANSPORT_SEND_TIMEOUT_US);
    while (!ep->is_completed) {
        if (time_reached(deadline)) {
            usb_cancel_transfer(ep);
            ep->data_buffer      = NULL;
            ep->data_buffer_size = 0;
            return TRANSPORT_ERR_IO;
        }
        tight_loop_contents();
    }

    ep->data_buffer      = NULL;
    ep->data_buffer_size = 0;

    return TRANSPORT_OK;
}

static transport_result_t usb_transport_receive_cb(void *user_data,
                                                     uint8_t *buf,
                                                     size_t len,
                                                     size_t *bytes_read)
{
    (void)user_data;
    *bytes_read = 0;

    if (!rx_ready) {
        /* Nothing available yet.  Returning TRANSPORT_OK with *bytes_read == 0
         * signals "no data yet" (polling contract) rather than EOF or error.
         * Callers (integration_process_one) treat zero bytes as a no-op. */
        return TRANSPORT_OK;
    }

    /* If the last incoming packet was truncated, the caller will receive a
     * partial packet that the protocol layer will likely reject.  Consume
     * and discard it; the host can retry. */
    size_t to_copy = (rx_len < len) ? rx_len : len;
    memcpy(buf, rx_buf, to_copy);
    *bytes_read  = to_copy;
    rx_ready     = false;
    rx_len       = 0;
    rx_truncated = false;

    return TRANSPORT_OK;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void usb_transport_init(void)
{
    usb_device_init();
}

const transport_interface_t usb_transport_iface = {
    .is_ready = usb_transport_is_ready_cb,
    .send     = usb_transport_send_cb,
    .receive  = usb_transport_receive_cb,
};
