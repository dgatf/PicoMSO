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

#include <config.h>
#include <glib.h>
#include "protocol.h"

#define PICOMSO_RESPONSE_TYPE_INFO PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_CAPABILITIES PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_STATUS PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_SET_MODE PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_REQUEST PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_DATA_BLOCK PICOMSO_MSG_DATA_BLOCK
#define PICOMSO_READ_DONE 1

static uint16_t read_u16_le(const uint8_t *data)
{
return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *data)
{
return (uint32_t)data[0] | ((uint32_t)data[1] << 8)
| ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
data[0] = (uint8_t)(value & 0xffu);
data[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
data[0] = (uint8_t)(value & 0xffu);
data[1] = (uint8_t)((value >> 8) & 0xffu);
data[2] = (uint8_t)((value >> 16) & 0xffu);
data[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void clear_error_state(struct dev_context *devc)
{
devc->last_device_status = (uint8_t)PICOMSO_STATUS_OK;
devc->last_error_text[0] = '\0';
}

static void set_error_text(struct dev_context *devc,
const char *text, size_t length)
{
size_t copy_len;

copy_len = length;
if (copy_len >= sizeof(devc->last_error_text))
copy_len = sizeof(devc->last_error_text) - 1u;

if (copy_len > 0u && text)
memcpy(devc->last_error_text, text, copy_len);
devc->last_error_text[copy_len] = '\0';
}

static int send_request(const struct sr_dev_inst *sdi,
uint8_t msg_type, const uint8_t *payload, uint16_t payload_len,
uint8_t expected_response_type, uint8_t *response,
size_t *response_len)
{
struct dev_context *devc;
struct sr_usb_dev_inst *usb;
uint8_t request[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
int actual_length, ret;
uint8_t seq;
uint16_t wire_length;
const uint8_t *payload_ptr;
uint8_t msg_len;

devc = sdi->priv;
usb = sdi->conn;

if (!response || !response_len || !usb || !usb->devhdl)
return SR_ERR_ARG;
if ((size_t)PICOMSO_PACKET_HEADER_SIZE + payload_len > sizeof(request))
return SR_ERR_ARG;

clear_error_state(devc);
seq = devc->next_seq++;

write_u16_le(request, PICOMSO_PACKET_MAGIC);
request[2] = PICOMSO_PROTOCOL_VERSION_MAJOR;
request[3] = PICOMSO_PROTOCOL_VERSION_MINOR;
request[4] = msg_type;
request[5] = seq;
write_u16_le(request + 6, payload_len);
if (payload_len > 0u && payload)
memcpy(request + PICOMSO_PACKET_HEADER_SIZE, payload, payload_len);

wire_length = PICOMSO_PACKET_HEADER_SIZE + payload_len;
ret = libusb_control_transfer(usb->devhdl,
LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
PICOMSO_CTRL_REQUEST_OUT, 0x0000, 0x0000, request, wire_length,
PICOMSO_USB_TIMEOUT_MS);
if (ret < 0 || ret != wire_length) {
sr_err("Unable to send request 0x%02x: %s.", msg_type,
ret < 0 ? libusb_error_name(ret) : "short control write");
return SR_ERR;
}

ret = libusb_bulk_transfer(usb->devhdl, PICOMSO_BULK_EP_IN, response,
PICOMSO_PROTOCOL_IO_BUFFER_SIZE, &actual_length,
PICOMSO_USB_TIMEOUT_MS);
if (ret < 0) {
sr_err("Unable to read response for request 0x%02x: %s.", msg_type,
libusb_error_name(ret));
return SR_ERR;
}

if ((size_t)actual_length < PICOMSO_PACKET_HEADER_SIZE)
return SR_ERR;
if (read_u16_le(response) != PICOMSO_PACKET_MAGIC)
return SR_ERR;
if (response[2] != PICOMSO_PROTOCOL_VERSION_MAJOR)
return SR_ERR;
if (response[5] != seq)
return SR_ERR;

*response_len = actual_length;
wire_length = read_u16_le(response + 6);
if ((size_t)PICOMSO_PACKET_HEADER_SIZE + wire_length > (size_t)actual_length)
return SR_ERR;

if (response[4] == PICOMSO_MSG_ERROR) {
payload_ptr = response + PICOMSO_PACKET_HEADER_SIZE;
if (wire_length < 2u)
return SR_ERR;
devc->last_device_status = payload_ptr[0];
msg_len = payload_ptr[1];
if ((uint16_t)(2u + msg_len) > wire_length)
return SR_ERR;
set_error_text(devc, (const char *)(payload_ptr + 2), msg_len);
return SR_ERR;
}

if (response[4] != expected_response_type)
return SR_ERR;

return SR_OK;
}

static int parse_ack_status(const uint8_t *response, size_t response_len)
{
size_t payload_len;
const uint8_t *payload;

if (!response || response_len < PICOMSO_PACKET_HEADER_SIZE)
return SR_ERR;

payload_len = read_u16_le(response + 6);
payload = response + PICOMSO_PACKET_HEADER_SIZE;
if (payload_len == 0u)
return SR_OK;
if (payload_len < 1u || response_len < PICOMSO_PACKET_HEADER_SIZE + payload_len)
return SR_ERR;
if (payload[0] != (uint8_t)PICOMSO_STATUS_OK)
return SR_ERR;

return SR_OK;
}

static int command_get_info(const struct sr_dev_inst *sdi,
picomso_info_response_t *info)
{
uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
size_t response_len;
size_t payload_len;
const uint8_t *payload;
int ret;

if (!info)
return SR_ERR_ARG;

ret = send_request(sdi, PICOMSO_MSG_GET_INFO, NULL, 0u,
PICOMSO_RESPONSE_TYPE_INFO, response, &response_len);
if (ret != SR_OK)
return ret;

payload_len = read_u16_le(response + 6);
if (payload_len < sizeof(*info))
return SR_ERR;

payload = response + PICOMSO_PACKET_HEADER_SIZE;
info->protocol_version_major = payload[0];
info->protocol_version_minor = payload[1];
memset(info->fw_id, 0, sizeof(info->fw_id));
memcpy(info->fw_id, payload + 2, sizeof(info->fw_id));
info->fw_id[sizeof(info->fw_id) - 1u] = '\0';

return SR_OK;
}

static int command_get_capabilities(const struct sr_dev_inst *sdi,
uint32_t *capabilities)
{
uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
size_t response_len;
const uint8_t *payload;
int ret;

if (!capabilities)
return SR_ERR_ARG;

ret = send_request(sdi, PICOMSO_MSG_GET_CAPABILITIES, NULL, 0u,
PICOMSO_RESPONSE_TYPE_CAPABILITIES, response, &response_len);
if (ret != SR_OK)
return ret;
if (read_u16_le(response + 6) < 4u)
return SR_ERR;

payload = response + PICOMSO_PACKET_HEADER_SIZE;
*capabilities = read_u32_le(payload);

return SR_OK;
}

static int command_get_status(const struct sr_dev_inst *sdi,
picomso_status_response_t *status)
{
uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
size_t response_len;
const uint8_t *payload;
int ret;

if (!status)
return SR_ERR_ARG;

ret = send_request(sdi, PICOMSO_MSG_GET_STATUS, NULL, 0u,
PICOMSO_RESPONSE_TYPE_STATUS, response, &response_len);
if (ret != SR_OK)
return ret;
if (read_u16_le(response + 6) < 2u)
return SR_ERR;

payload = response + PICOMSO_PACKET_HEADER_SIZE;
status->mode = payload[0];
status->capture_state = payload[1];

return SR_OK;
}

static int command_set_mode(const struct sr_dev_inst *sdi,
picomso_device_mode_t mode)
{
uint8_t payload[1];
uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
size_t response_len;
int ret;

payload[0] = (uint8_t)mode;
ret = send_request(sdi, PICOMSO_MSG_SET_MODE, payload, sizeof(payload),
PICOMSO_RESPONSE_TYPE_SET_MODE, response, &response_len);
if (ret != SR_OK)
return ret;

return parse_ack_status(response, response_len);
}

static int command_request_capture(const struct sr_dev_inst *sdi,
const picomso_request_capture_request_t *request)
{
uint8_t payload[sizeof(*request)];
uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
size_t response_len;
unsigned int i;
size_t offset;
int ret;

if (!request)
return SR_ERR_ARG;

write_u32_le(payload, request->total_samples);
write_u32_le(payload + 4, request->rate);
write_u32_le(payload + 8, request->pre_trigger_samples);
for (i = 0; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; i++) {
offset = 12u + (size_t)i * 3u;
payload[offset] = request->trigger[i].is_enabled;
payload[offset + 1u] = request->trigger[i].pin;
payload[offset + 2u] = request->trigger[i].match;
}

ret = send_request(sdi, PICOMSO_MSG_REQUEST_CAPTURE, payload,
(uint16_t)sizeof(payload), PICOMSO_RESPONSE_TYPE_REQUEST,
response, &response_len);
if (ret != SR_OK)
return ret;

return parse_ack_status(response, response_len);
}

static int command_read_data_block(const struct sr_dev_inst *sdi,
picomso_data_block_response_t *block)
{
struct dev_context *devc;
uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
size_t response_len;
uint16_t payload_len;
const uint8_t *payload;
int ret;

devc = sdi->priv;
if (!block)
return SR_ERR_ARG;

memset(block, 0, sizeof(*block));
ret = send_request(sdi, PICOMSO_MSG_READ_DATA_BLOCK, NULL, 0u,
PICOMSO_RESPONSE_TYPE_DATA_BLOCK, response, &response_len);
if (ret != SR_OK
&& devc->last_device_status == (uint8_t)PICOMSO_STATUS_ERR_UNKNOWN
&& !strcmp(devc->last_error_text, "no finalized capture data"))
return PICOMSO_READ_DONE;
if (ret != SR_OK)
return ret;

payload_len = read_u16_le(response + 6);
if (payload_len < 4u)
return SR_ERR;

payload = response + PICOMSO_PACKET_HEADER_SIZE;
block->block_id = read_u16_le(payload);
block->data_len = read_u16_le(payload + 2);
if (block->data_len > PICOMSO_DATA_BLOCK_SIZE)
return SR_ERR;
if ((uint16_t)(4u + block->data_len) > payload_len)
return SR_ERR;
if (block->data_len > 0u)
memcpy(block->data, payload + 4, block->data_len);

(void)response_len;
return SR_OK;
}

static int trigger_match_to_picomso(enum sr_trigger_matches match,
uint8_t *picomso_match)
{
switch (match) {
case SR_TRIGGER_ZERO:
*picomso_match = PICOMSO_TRIGGER_MATCH_LEVEL_LOW;
break;
case SR_TRIGGER_ONE:
*picomso_match = PICOMSO_TRIGGER_MATCH_LEVEL_HIGH;
break;
case SR_TRIGGER_RISING:
*picomso_match = PICOMSO_TRIGGER_MATCH_EDGE_HIGH;
break;
case SR_TRIGGER_FALLING:
*picomso_match = PICOMSO_TRIGGER_MATCH_EDGE_LOW;
break;
default:
return SR_ERR_NA;
}

return SR_OK;
}

static int build_capture_request(const struct sr_dev_inst *sdi,
picomso_request_capture_request_t *request)
{
struct dev_context *devc;
struct sr_trigger *trigger;
struct sr_trigger_stage *stage;
struct sr_trigger_match *match;
const GSList *l;
uint64_t total_samples;
uint64_t pre_trigger_samples;
unsigned int trigger_index;
int ret;

devc = sdi->priv;
memset(request, 0, sizeof(*request));

total_samples = devc->limit_samples ? devc->limit_samples
: PICOMSO_DEFAULT_LIMIT_SAMPLES;
if (total_samples == 0 || total_samples > PICOMSO_MAX_TOTAL_SAMPLES)
return SR_ERR_ARG;
if (devc->cur_samplerate == 0)
return SR_ERR_ARG;
if (devc->capture_ratio > 100)
return SR_ERR_ARG;

pre_trigger_samples = (devc->capture_ratio * total_samples) / 100;
if (pre_trigger_samples > PICOMSO_MAX_PRE_TRIGGER_SAMPLES)
return SR_ERR_ARG;
if (total_samples - pre_trigger_samples > PICOMSO_MAX_POST_TRIGGER_SAMPLES)
return SR_ERR_ARG;

request->total_samples = total_samples;
request->rate = devc->cur_samplerate;
request->pre_trigger_samples = pre_trigger_samples;

trigger = sr_session_trigger_get(sdi->session);
if (!trigger)
return SR_OK;
if (g_slist_length(trigger->stages) > 1)
return SR_ERR_NA;

stage = g_slist_nth_data(trigger->stages, 0);
if (!stage)
return SR_ERR_ARG;

trigger_index = 0;
for (l = stage->matches; l; l = l->next) {
match = l->data;
if (!match->match || !match->channel || !match->channel->enabled)
continue;
if (match->channel->index >= NUM_CHANNELS)
return SR_ERR_ARG;
if (trigger_index >= PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT)
return SR_ERR_NA;

ret = trigger_match_to_picomso(match->match,
&request->trigger[trigger_index].match);
if (ret != SR_OK)
return ret;
request->trigger[trigger_index].is_enabled = 1u;
request->trigger[trigger_index].pin = match->channel->index;
trigger_index++;
}

return SR_OK;
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
struct dev_context *devc;

devc = sdi->priv;
if (devc->acq_state == PICOMSO_ACQ_IDLE)
return;

if (command_set_mode(sdi, PICOMSO_MODE_UNSET) != SR_OK)
sr_dbg("Unable to switch device back to unset mode after acquisition.");

sr_session_source_remove(sdi->session, -1);
std_session_send_df_end(sdi);

devc->acq_aborted = FALSE;
devc->acq_state = PICOMSO_ACQ_IDLE;
devc->expected_block_id = 0;
devc->capture_deadline_us = 0;
}

static int send_logic_data(struct sr_dev_inst *sdi,
const picomso_data_block_response_t *block)
{
struct dev_context *devc;
struct sr_datafeed_logic logic;
struct sr_datafeed_packet packet;
size_t sample_count;
size_t max_sample_count;

devc = sdi->priv;
if ((block->data_len % sizeof(uint16_t)) != 0u)
return SR_ERR;

sample_count = block->data_len / sizeof(uint16_t);
if (devc->limit_samples
&& devc->sent_samples + sample_count > devc->limit_samples) {
max_sample_count = devc->limit_samples - devc->sent_samples;
sample_count = max_sample_count;
}
if (sample_count == 0u)
return SR_OK;

logic.length = sample_count * sizeof(uint16_t);
logic.unitsize = sizeof(uint16_t);
logic.data = (uint8_t *)block->data;
packet.type = SR_DF_LOGIC;
packet.payload = &logic;
devc->sent_samples += sample_count;

return sr_session_send(sdi, &packet);
}

static int receive_data(int fd, int revents, void *cb_data)
{
struct sr_dev_inst *sdi;
struct dev_context *devc;
picomso_status_response_t status;
picomso_data_block_response_t block;
int ret;

(void)fd;
(void)revents;

sdi = cb_data;
devc = sdi->priv;

if (devc->acq_aborted) {
finish_acquisition(sdi);
return FALSE;
}

if (devc->acq_state == PICOMSO_ACQ_WAITING) {
ret = command_get_status(sdi, &status);
if (ret != SR_OK) {
sr_err("Failed to query PicoMSO capture status.");
finish_acquisition(sdi);
return FALSE;
}
if (status.mode != PICOMSO_MODE_LOGIC) {
sr_err("Device left logic mode while capture was running.");
finish_acquisition(sdi);
return FALSE;
}
if (status.capture_state == PICOMSO_CAPTURE_RUNNING) {
if (devc->capture_deadline_us > 0
&& g_get_monotonic_time() > devc->capture_deadline_us) {
sr_err("Timed out waiting for PicoMSO capture completion.");
finish_acquisition(sdi);
return FALSE;
}
return TRUE;
}
if (status.capture_state != PICOMSO_CAPTURE_IDLE) {
sr_err("Unexpected PicoMSO capture state 0x%02x.",
status.capture_state);
finish_acquisition(sdi);
return FALSE;
}

devc->acq_state = PICOMSO_ACQ_READING;
}

while (!devc->acq_aborted) {
ret = command_read_data_block(sdi, &block);
if (ret == PICOMSO_READ_DONE) {
finish_acquisition(sdi);
return FALSE;
}
if (ret != SR_OK) {
sr_err("Failed to read finalized PicoMSO capture data.");
finish_acquisition(sdi);
return FALSE;
}
if (block.block_id != devc->expected_block_id) {
sr_err("Unexpected PicoMSO block id %u, expected %u.",
block.block_id, devc->expected_block_id);
finish_acquisition(sdi);
return FALSE;
}
ret = send_logic_data(sdi, &block);
if (ret != SR_OK) {
sr_err("Failed to forward PicoMSO logic data.");
finish_acquisition(sdi);
return FALSE;
}
devc->expected_block_id++;
}

finish_acquisition(sdi);
return FALSE;
}

SR_PRIV int picomso_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di)
{
libusb_device **devlist;
struct sr_usb_dev_inst *usb;
struct libusb_device_descriptor des;
struct dev_context *devc;
struct drv_context *drvc;
int ret, i, device_count;
char connection_id[64];

drvc = di->context;
devc = sdi->priv;
usb = sdi->conn;

device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
if (device_count < 0) {
sr_err("Failed to get device list: %s.",
libusb_error_name(device_count));
return SR_ERR;
}

ret = SR_ERR;
for (i = 0; i < device_count; i++) {
libusb_get_device_descriptor(devlist[i], &des);
if (des.idVendor != devc->profile->vid
|| des.idProduct != devc->profile->pid)
continue;
if (usb_get_port_path(devlist[i], connection_id,
sizeof(connection_id)) < 0)
continue;
if (strcmp(sdi->connection_id, connection_id))
continue;

ret = libusb_open(devlist[i], &usb->devhdl);
if (ret < 0) {
sr_err("Failed to open device: %s.", libusb_error_name(ret));
ret = SR_ERR;
break;
}
if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER)
&& libusb_kernel_driver_active(usb->devhdl,
USB_INTERFACE) == 1) {
ret = libusb_detach_kernel_driver(usb->devhdl, USB_INTERFACE);
if (ret < 0) {
sr_err("Failed to detach kernel driver: %s.",
libusb_error_name(ret));
libusb_close(usb->devhdl);
usb->devhdl = NULL;
ret = SR_ERR;
break;
}
}
ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
if (ret != 0) {
sr_err("Unable to claim interface: %s.",
libusb_error_name(ret));
libusb_close(usb->devhdl);
usb->devhdl = NULL;
ret = SR_ERR;
break;
}

ret = command_get_info(sdi, &devc->info);
if (ret != SR_OK) {
sr_err("Failed to query PicoMSO device information.");
libusb_release_interface(usb->devhdl, USB_INTERFACE);
libusb_close(usb->devhdl);
usb->devhdl = NULL;
break;
}
ret = command_get_capabilities(sdi, &devc->capabilities);
if (ret != SR_OK) {
sr_err("Failed to query PicoMSO device capabilities.");
libusb_release_interface(usb->devhdl, USB_INTERFACE);
libusb_close(usb->devhdl);
usb->devhdl = NULL;
break;
}
if ((devc->capabilities & PICOMSO_CAP_LOGIC) == 0u) {
sr_err("Connected PicoMSO device does not expose logic capability.");
libusb_release_interface(usb->devhdl, USB_INTERFACE);
libusb_close(usb->devhdl);
usb->devhdl = NULL;
ret = SR_ERR;
break;
}

sr_info("Opened PicoMSO device on %d.%d / %s, firmware %u.%u (%s).",
usb->bus, usb->address, connection_id,
devc->info.protocol_version_major,
devc->info.protocol_version_minor, devc->info.fw_id);
ret = SR_OK;
break;
}

libusb_free_device_list(devlist, 1);
return ret;
}

SR_PRIV struct dev_context *picomso_dev_new(void)
{
struct dev_context *devc;

devc = g_malloc0(sizeof(struct dev_context));
devc->profile = NULL;
devc->channel_names = NULL;
devc->cur_samplerate = 0;
devc->limit_samples = PICOMSO_DEFAULT_LIMIT_SAMPLES;
devc->capture_ratio = 0;
devc->sent_samples = 0;
devc->next_seq = 1u;
devc->last_device_status = PICOMSO_STATUS_OK;
devc->acq_state = PICOMSO_ACQ_IDLE;
devc->expected_block_id = 0;
devc->capture_deadline_us = 0;
clear_error_state(devc);

return devc;
}

SR_PRIV void picomso_abort_acquisition(struct dev_context *devc)
{
devc->acq_aborted = TRUE;
}

SR_PRIV int picomso_start_acquisition(const struct sr_dev_inst *sdi)
{
struct dev_context *devc;
picomso_request_capture_request_t request;
picomso_status_response_t status;
int ret;
gint64 capture_time_us;

devc = sdi->priv;
if (devc->acq_state != PICOMSO_ACQ_IDLE)
return SR_ERR;

ret = build_capture_request(sdi, &request);
if (ret != SR_OK)
return ret;

ret = command_set_mode(sdi, PICOMSO_MODE_LOGIC);
if (ret != SR_OK)
return ret;

ret = command_get_status(sdi, &status);
if (ret != SR_OK)
return ret;
if (status.mode != PICOMSO_MODE_LOGIC
|| status.capture_state == PICOMSO_CAPTURE_RUNNING)
return SR_ERR;

ret = command_request_capture(sdi, &request);
if (ret != SR_OK)
return ret;

devc->acq_aborted = FALSE;
devc->acq_state = PICOMSO_ACQ_WAITING;
devc->expected_block_id = 0;
devc->sent_samples = 0;
capture_time_us = ((gint64)request.total_samples * G_USEC_PER_SEC)
/ request.rate;
devc->capture_deadline_us = g_get_monotonic_time() + capture_time_us
+ (3 * G_USEC_PER_SEC);

ret = sr_session_source_add(sdi->session, -1, 0,
PICOMSO_POLL_INTERVAL_MS, receive_data, (void *)sdi);
if (ret != SR_OK) {
devc->acq_state = PICOMSO_ACQ_IDLE;
command_set_mode(sdi, PICOMSO_MODE_UNSET);
return ret;
}

ret = std_session_send_df_header(sdi);
if (ret != SR_OK) {
sr_session_source_remove(sdi->session, -1);
devc->acq_state = PICOMSO_ACQ_IDLE;
command_set_mode(sdi, PICOMSO_MODE_UNSET);
}

return ret;
}
