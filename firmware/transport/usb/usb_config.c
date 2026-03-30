/**
 * Copyright (c) 2024 Daniel Gorbea
 *
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd. author of https://github.com/raspberrypi/pico-examples/tree/master/usb
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * PicoMSO-specific USB device configuration source.
 *
 * This file is #included by usb.c (not compiled as a standalone translation
 * unit).  It instantiates the USB device configuration structure (dev_config)
 * and the EP0 data buffer for the PicoMSO device.
 *
 * control_transfer_handler() must be provided by the application layer that
 * links against picomso_usb_transport.  Its signature matches:
 *
 *   void control_transfer_handler(uint8_t *buf,
 *                                 volatile struct usb_setup_packet *pkt,
 *                                 uint8_t stage);
 */

#include "usb_config.h"

static uint8_t ep0_buf[256];

void control_transfer_handler(uint8_t *buf, volatile struct usb_setup_packet *pkt, uint8_t stage);

static struct usb_device_configuration dev_config = {
    .device_descriptor = &device_descriptor,
    .interface_descriptor = &interface_descriptor,
    .config_descriptor = &config_descriptor,
    .lang_descriptor = lang_descriptor,
    .descriptor_strings = descriptor_strings,
    .control_transfer_handler = &control_transfer_handler,
    .endpoints = {{
                      .descriptor = &ep0_out,
                      .double_buffer = false,  // Double buffer not supported for EP0
                      .data_buffer = ep0_buf,
                      .data_buffer_size = sizeof(ep0_buf),
                  },
                  {
                      .descriptor = &ep0_in,
                      .double_buffer = false,  // Double buffer not supported for EP0
                      .data_buffer = ep0_buf,
                      .data_buffer_size = sizeof(ep0_buf),
                  },
                  {
                      .descriptor = &ep6_in,
                      .handler = NULL,
                      .double_buffer = true,
                      .data_buffer = NULL,
                  }}};
