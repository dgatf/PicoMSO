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

#include "api.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef enum {
    TEST_SCENARIO_END_ON_DEVICE_ERROR = 0,
    TEST_SCENARIO_SHORT_BLOCK_BEFORE_END,
} test_scenario_t;

typedef struct {
    test_scenario_t scenario;
    picomso_sigrok_driver_t driver;
    size_t total_samples_seen;
    uint16_t last_sample;
    uint16_t request_sequence[16];
    size_t request_count;
    size_t data_block_count;
    bool capture_requested;
} fake_device_t;

static size_t build_ack_with_payload(uint8_t *buf,
                                     size_t buf_len,
                                     uint8_t seq,
                                     const void *payload,
                                     size_t payload_len)
{
    return picomso_sigrok_build_request(buf, buf_len, PICOMSO_MSG_ACK, seq, payload, payload_len);
}

static size_t build_error_response(uint8_t *buf,
                                   size_t buf_len,
                                   uint8_t seq,
                                   picomso_status_t status,
                                   const char *message)
{
    picomso_packet_header_t header;
    picomso_error_payload_t error_payload;
    size_t message_len = strlen(message);
    size_t total_len;

    if (message_len > 255u) {
        message_len = 255u;
    }

    total_len = sizeof(header) + sizeof(error_payload) + message_len;
    assert(buf_len >= total_len);

    header.magic = PICOMSO_PACKET_MAGIC;
    header.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    header.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    header.msg_type = PICOMSO_MSG_ERROR;
    header.seq = seq;
    header.length = (uint16_t)(sizeof(error_payload) + message_len);
    memcpy(buf, &header, sizeof(header));

    error_payload.status = (uint8_t)status;
    error_payload.msg_len = (uint8_t)message_len;
    memcpy(buf + sizeof(header), &error_payload, sizeof(error_payload));
    memcpy(buf + sizeof(header) + sizeof(error_payload), message, message_len);
    return total_len;
}

static size_t build_data_block_response(uint8_t *buf,
                                        size_t buf_len,
                                        uint8_t seq,
                                        uint16_t block_id,
                                        const uint16_t *samples,
                                        size_t sample_count)
{
    picomso_packet_header_t header;
    picomso_data_block_response_t block;
    size_t byte_count = sample_count * sizeof(uint16_t);
    size_t total_len;

    assert(byte_count <= PICOMSO_DATA_BLOCK_SIZE);
    total_len = sizeof(header) + 4u + byte_count;
    assert(buf_len >= total_len);

    header.magic = PICOMSO_PACKET_MAGIC;
    header.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    header.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    header.msg_type = PICOMSO_MSG_DATA_BLOCK;
    header.seq = seq;
    header.length = (uint16_t)(4u + byte_count);
    memcpy(buf, &header, sizeof(header));

    memset(&block, 0, sizeof(block));
    block.block_id = block_id;
    block.data_len = (uint16_t)byte_count;
    memcpy(block.data, samples, byte_count);
    memcpy(buf + sizeof(header), &block, 4u + byte_count);
    return total_len;
}

static size_t build_data_block_response_bytes(uint8_t *buf,
                                              size_t buf_len,
                                              uint8_t seq,
                                              uint16_t block_id,
                                              const uint8_t *data,
                                              size_t byte_count)
{
    picomso_packet_header_t header;
    picomso_data_block_response_t block;
    size_t total_len;

    assert(byte_count <= PICOMSO_DATA_BLOCK_SIZE);
    total_len = sizeof(header) + 4u + byte_count;
    assert(buf_len >= total_len);

    header.magic = PICOMSO_PACKET_MAGIC;
    header.version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
    header.version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
    header.msg_type = PICOMSO_MSG_DATA_BLOCK;
    header.seq = seq;
    header.length = (uint16_t)(4u + byte_count);
    memcpy(buf, &header, sizeof(header));

    memset(&block, 0, sizeof(block));
    block.block_id = block_id;
    block.data_len = (uint16_t)byte_count;
    memcpy(block.data, data, byte_count);
    memcpy(buf + sizeof(header), &block, 4u + byte_count);
    return total_len;
}

static bool fake_send_control(void *user_data, const uint8_t *buf, size_t len)
{
    fake_device_t *device = (fake_device_t *)user_data;
    picomso_packet_header_t header;

    assert(len >= sizeof(header));
    memcpy(&header, buf, sizeof(header));
    assert(device->request_count < ARRAY_SIZE(device->request_sequence));
    device->request_sequence[device->request_count++] = header.msg_type;
    if (header.msg_type == PICOMSO_MSG_REQUEST_CAPTURE) {
        device->capture_requested = true;
    }
    return true;
}

static bool fake_recv_bulk(void *user_data, uint8_t *buf, size_t buf_capacity, size_t *received_len)
{
    fake_device_t *device = (fake_device_t *)user_data;
    uint8_t seq = (uint8_t)device->request_count;
    uint8_t last_request = (uint8_t)device->request_sequence[device->request_count - 1u];

    if (last_request == PICOMSO_MSG_GET_INFO) {
        picomso_info_response_t info;
        memset(&info, 0, sizeof(info));
        info.protocol_version_major = PICOMSO_PROTOCOL_VERSION_MAJOR;
        info.protocol_version_minor = PICOMSO_PROTOCOL_VERSION_MINOR;
        memcpy(info.fw_id, "PicoMSO-0.1", sizeof("PicoMSO-0.1") - 1u);
        *received_len = build_ack_with_payload(buf, buf_capacity, seq, &info, sizeof(info));
        return true;
    }

    if (last_request == PICOMSO_MSG_GET_CAPABILITIES) {
        picomso_capabilities_response_t caps;
        caps.capabilities = PICOMSO_CAP_LOGIC;
        *received_len = build_ack_with_payload(buf, buf_capacity, seq, &caps, sizeof(caps));
        return true;
    }

    if (last_request == PICOMSO_MSG_SET_MODE) {
        picomso_ack_payload_t ack;
        ack.status = PICOMSO_STATUS_OK;
        *received_len = build_ack_with_payload(buf, buf_capacity, seq, &ack, sizeof(ack));
        return true;
    }

    if (last_request == PICOMSO_MSG_GET_STATUS) {
        picomso_status_response_t status;
        status.mode = PICOMSO_MODE_LOGIC;
        if (!device->capture_requested) {
            status.capture_state = PICOMSO_CAPTURE_IDLE;
        } else if (device->data_block_count == 0u) {
            status.capture_state = PICOMSO_CAPTURE_RUNNING;
            device->data_block_count = 1u;
        } else {
            status.capture_state = PICOMSO_CAPTURE_IDLE;
        }
        *received_len = build_ack_with_payload(buf, buf_capacity, seq, &status, sizeof(status));
        return true;
    }

    if (last_request == PICOMSO_MSG_REQUEST_CAPTURE) {
        picomso_ack_payload_t ack;
        ack.status = PICOMSO_STATUS_OK;
        *received_len = build_ack_with_payload(buf, buf_capacity, seq, &ack, sizeof(ack));
        device->data_block_count = 0u;
        return true;
    }

    if (last_request == PICOMSO_MSG_READ_DATA_BLOCK) {
        if (device->scenario == TEST_SCENARIO_END_ON_DEVICE_ERROR) {
            static const uint16_t samples[] = {
                0x0001u, 0x0002u, 0x0003u, 0x0004u, 0x0005u, 0x0006u, 0x0007u, 0x0008u,
                0x0009u, 0x000Au, 0x000Bu, 0x000Cu, 0x000Du, 0x000Eu, 0x000Fu, 0x0010u,
                0x0011u, 0x0012u, 0x0013u, 0x0014u, 0x0015u, 0x0016u, 0x0017u, 0x0018u,
                0x0019u, 0x001Au, 0x001Bu, 0x001Cu, 0x001Du, 0x001Eu, 0x001Fu, 0x0020u,
            };

            if (device->data_block_count == 1u) {
                *received_len = build_data_block_response(buf, buf_capacity, seq, 0u, samples, ARRAY_SIZE(samples));
                device->data_block_count = 2u;
                return true;
            }

            *received_len = build_error_response(buf, buf_capacity, seq, PICOMSO_STATUS_ERR_UNKNOWN,
                                                 PICOMSO_SIGROK_END_OF_CAPTURE_MESSAGE);
            return true;
        }

        if (device->scenario == TEST_SCENARIO_SHORT_BLOCK_BEFORE_END) {
            static const uint16_t block0[] = {
                0x0101u, 0x0102u, 0x0103u, 0x0104u, 0x0105u, 0x0106u, 0x0107u, 0x0108u,
                0x0109u, 0x010Au, 0x010Bu, 0x010Cu, 0x010Du, 0x010Eu, 0x010Fu, 0x0110u,
                0x0111u, 0x0112u, 0x0113u, 0x0114u, 0x0115u, 0x0116u, 0x0117u, 0x0118u,
                0x0119u, 0x011Au, 0x011Bu, 0x011Cu, 0x011Du, 0x011Eu, 0x011Fu, 0x0120u,
            };
            static const uint8_t block1[] = {
                0x01u, 0x20u,
                0x02u, 0x20u,
                0x03u, 0x20u,
                0x04u, 0x20u,
            };
            static const uint16_t block2[] = {
                0x0301u, 0x0302u,
            };

            if (device->data_block_count == 1u) {
                *received_len = build_data_block_response(buf, buf_capacity, seq, 0u, block0, ARRAY_SIZE(block0));
                device->data_block_count = 2u;
                return true;
            }

            if (device->data_block_count == 2u) {
                *received_len = build_data_block_response_bytes(buf, buf_capacity, seq, 1u, block1, sizeof(block1));
                device->data_block_count = 3u;
                return true;
            }

            if (device->data_block_count == 3u) {
                *received_len = build_data_block_response(buf, buf_capacity, seq, 2u, block2, ARRAY_SIZE(block2));
                device->data_block_count = 4u;
                return true;
            }

            *received_len = build_error_response(buf, buf_capacity, seq, PICOMSO_STATUS_ERR_UNKNOWN,
                                                 PICOMSO_SIGROK_END_OF_CAPTURE_MESSAGE);
            return true;
        }
    }

    return false;
}

static void fake_sleep_ms(void *user_data, uint32_t milliseconds)
{
    (void)user_data;
    (void)milliseconds;
}

static bool collect_samples(void *user_data, const uint16_t *samples, size_t sample_count)
{
    fake_device_t *device = (fake_device_t *)user_data;

    if (sample_count > 0u) {
        device->last_sample = samples[sample_count - 1u];
    }
    device->total_samples_seen += sample_count;
    return true;
}

static void init_fake_device(fake_device_t *device, test_scenario_t scenario)
{
    static const picomso_sigrok_transport_ops_t ops = {
        .send_control = fake_send_control,
        .recv_bulk = fake_recv_bulk,
        .sleep_ms = fake_sleep_ms,
    };

    memset(device, 0, sizeof(*device));
    device->scenario = scenario;
    picomso_sigrok_driver_init(&device->driver, &ops, device);
}

static void test_end_on_device_error(void)
{
    fake_device_t device;
    picomso_sigrok_logic_config_t config;
    size_t captured_samples = 0u;
    picomso_sigrok_status_t status;

    init_fake_device(&device, TEST_SCENARIO_END_ON_DEVICE_ERROR);
    status = picomso_sigrok_dev_open(&device.driver);
    assert(status == PICOMSO_SIGROK_OK);

    memset(&config, 0, sizeof(config));
    config.total_samples = 256u;
    config.sample_rate_hz = 100000u;
    config.pre_trigger_samples = 64u;
    config.poll_interval_ms = 1u;
    config.poll_timeout_ms = 10u;

    status = picomso_sigrok_acquisition_start(&device.driver, &config, collect_samples, &device,
                                              &captured_samples);
    assert(status == PICOMSO_SIGROK_OK);
    assert(captured_samples == 32u);
    assert(device.total_samples_seen == 32u);
    assert(device.last_sample == 0x0020u);
    assert(device.request_sequence[0] == PICOMSO_MSG_GET_INFO);
    assert(device.request_sequence[1] == PICOMSO_MSG_GET_CAPABILITIES);
    assert(device.request_sequence[2] == PICOMSO_MSG_SET_MODE);
    assert(device.request_sequence[3] == PICOMSO_MSG_GET_STATUS);
    assert(device.request_sequence[4] == PICOMSO_MSG_REQUEST_CAPTURE);
}

static void test_short_block_before_end(void)
{
    fake_device_t device;
    picomso_sigrok_logic_config_t config;
    size_t captured_samples = 0u;
    picomso_sigrok_status_t status;

    init_fake_device(&device, TEST_SCENARIO_SHORT_BLOCK_BEFORE_END);
    status = picomso_sigrok_dev_open(&device.driver);
    assert(status == PICOMSO_SIGROK_OK);

    memset(&config, 0, sizeof(config));
    config.total_samples = 512u;
    config.sample_rate_hz = 100000u;
    config.pre_trigger_samples = 128u;
    config.poll_interval_ms = 1u;
    config.poll_timeout_ms = 10u;

    status = picomso_sigrok_acquisition_start(&device.driver, &config, collect_samples, &device,
                                              &captured_samples);
    assert(status == PICOMSO_SIGROK_OK);
    assert(captured_samples == 38u);
    assert(device.total_samples_seen == 38u);
    assert(device.last_sample == 0x0302u);
}

int main(void)
{
    test_end_on_device_error();
    test_short_block_before_end();
    puts("picomso_sigrok_driver_test: OK");
    return EXIT_SUCCESS;
}
