/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026
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

#ifndef LIBSIGROK_HARDWARE_PICOMSO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_PICOMSO_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#include "../../../firmware/protocol/include/protocol.h"
#include "../../../firmware/protocol/include/protocol_packets.h"

#define LOG_PREFIX "picomso"

#define USB_INTERFACE 0
#define USB_CONFIGURATION 1
#define NUM_CHANNELS 16

#define PICOMSO_USB_TIMEOUT_MS 500
#define PICOMSO_POLL_INTERVAL_MS 10
#define PICOMSO_PROTOCOL_IO_BUFFER_SIZE 256u
#define PICOMSO_PROTOCOL_ERROR_TEXT_MAX 64u
#define PICOMSO_BULK_EP_IN 0x86
#define PICOMSO_CTRL_REQUEST_OUT 0x01
#define PICOMSO_DEFAULT_LIMIT_SAMPLES 1024u
#define PICOMSO_MAX_PRE_TRIGGER_SAMPLES 1024u
#define PICOMSO_MAX_POST_TRIGGER_SAMPLES 10000u
#define PICOMSO_MAX_TOTAL_SAMPLES \
    (PICOMSO_MAX_PRE_TRIGGER_SAMPLES + PICOMSO_MAX_POST_TRIGGER_SAMPLES)

struct picomso_profile {
    uint16_t vid;
    uint16_t pid;

    const char *vendor;
    const char *model;
    const char *model_version;

    const char *usb_manufacturer;
    const char *usb_product;
};

enum picomso_acq_state {
    PICOMSO_ACQ_IDLE,
    PICOMSO_ACQ_WAITING,
    PICOMSO_ACQ_READING,
};

struct dev_context {
    const struct picomso_profile *profile;
    char **channel_names;

    const uint64_t *samplerates;
    int num_samplerates;

    uint64_t cur_samplerate;
    uint64_t limit_samples;
    uint64_t capture_ratio;
    uint64_t sent_samples;

    uint32_t capabilities;
    picomso_info_response_t info;

    uint8_t next_seq;
    uint8_t last_device_status;
    char last_error_text[PICOMSO_PROTOCOL_ERROR_TEXT_MAX];

    gboolean acq_aborted;
    enum picomso_acq_state acq_state;
    uint16_t expected_block_id;
    gint64 capture_deadline_us;
};

SR_PRIV int picomso_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di);
SR_PRIV struct dev_context *picomso_dev_new(void);
SR_PRIV int picomso_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV void picomso_abort_acquisition(struct dev_context *devc);

#endif
