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

#include <stdio.h>
#include <string.h>

#include "protocol.h"

#define PICOMSO_SIGROK_RX_BUF_SIZE  PICOMSO_SIGROK_PACKET_BUF_SIZE

static void set_error(picomso_sigrok_driver_t *driver, const char *message)
{
    if (driver == NULL) {
        return;
    }

    snprintf(driver->last_error, sizeof(driver->last_error), "%s",
             (message != NULL) ? message : "unknown PicoMSO driver error");
}

static picomso_sigrok_status_t forward_logic_samples(picomso_sigrok_driver_t *driver,
                                                     picomso_sigrok_samples_cb_t samples_cb,
                                                     void *samples_cb_data,
                                                     const uint8_t *data,
                                                     uint16_t data_len,
                                                     size_t *captured_samples)
{
    uint16_t samples[PICOMSO_DATA_BLOCK_SIZE / sizeof(uint16_t)];
    size_t sample_count;
    size_t i;

    if ((data_len % sizeof(uint16_t)) != 0u) {
        set_error(driver, "PicoMSO logic data block has odd byte length");
        return PICOMSO_SIGROK_ERR_PROTOCOL;
    }

    sample_count = data_len / sizeof(uint16_t);
    for (i = 0; i < sample_count; i++) {
        size_t offset = i * sizeof(uint16_t);
        samples[i] = (uint16_t)data[offset] | (uint16_t)((uint16_t)data[offset + 1u] << 8);
    }

    if (sample_count > 0u && !samples_cb(samples_cb_data, samples, sample_count)) {
        set_error(driver, "sample callback rejected PicoMSO logic data block");
        return PICOMSO_SIGROK_ERR_CALLBACK;
    }

    *captured_samples += sample_count;
    return PICOMSO_SIGROK_OK;
}

static picomso_sigrok_status_t transport_round_trip(picomso_sigrok_driver_t *driver,
                                                    picomso_msg_type_t msg_type,
                                                    const void *payload,
                                                    size_t payload_len,
                                                    picomso_sigrok_response_t *response)
{
    uint8_t tx_buf[PICOMSO_SIGROK_PACKET_BUF_SIZE];
    uint8_t rx_buf[PICOMSO_SIGROK_RX_BUF_SIZE];
    size_t tx_len;
    size_t rx_len = 0u;
    char parse_error[PICOMSO_SIGROK_ERROR_TEXT_MAX];
    uint8_t seq;

    if (driver == NULL || response == NULL || driver->transport_ops == NULL ||
        driver->transport_ops->send_control == NULL || driver->transport_ops->recv_bulk == NULL) {
        return PICOMSO_SIGROK_ERR_ARG;
    }

    seq = driver->next_seq++;
    tx_len = picomso_sigrok_build_request(tx_buf, sizeof(tx_buf), msg_type, seq, payload, payload_len);
    if (tx_len == 0u) {
        set_error(driver, "failed to build PicoMSO request packet");
        return PICOMSO_SIGROK_ERR_PROTOCOL;
    }

    if (!driver->transport_ops->send_control(driver->transport_data, tx_buf, tx_len)) {
        set_error(driver, "PicoMSO control transfer failed");
        return PICOMSO_SIGROK_ERR_TRANSPORT;
    }

    if (!driver->transport_ops->recv_bulk(driver->transport_data, rx_buf, sizeof(rx_buf), &rx_len)) {
        set_error(driver, "PicoMSO bulk read failed");
        return PICOMSO_SIGROK_ERR_TRANSPORT;
    }

    if (!picomso_sigrok_parse_response(rx_buf, rx_len, seq, response, parse_error, sizeof(parse_error))) {
        set_error(driver, parse_error);
        return PICOMSO_SIGROK_ERR_PROTOCOL;
    }

    return PICOMSO_SIGROK_OK;
}

static picomso_sigrok_status_t expect_ack_only(picomso_sigrok_driver_t *driver,
                                               picomso_msg_type_t msg_type,
                                               const void *payload,
                                               size_t payload_len)
{
    picomso_sigrok_response_t response;
    picomso_sigrok_device_error_t device_error;
    picomso_sigrok_status_t status;

    status = transport_round_trip(driver, msg_type, payload, payload_len, &response);
    if (status != PICOMSO_SIGROK_OK) {
        return status;
    }

    if (response.header.msg_type == PICOMSO_MSG_ERROR) {
        if (!picomso_sigrok_decode_device_error(&response, &device_error, driver->last_error,
                                                sizeof(driver->last_error))) {
            set_error(driver, "failed to decode PicoMSO ERROR packet");
            return PICOMSO_SIGROK_ERR_PROTOCOL;
        }
        return PICOMSO_SIGROK_ERR_DEVICE;
    }

    if (response.header.msg_type != PICOMSO_MSG_ACK) {
        set_error(driver, "expected PicoMSO ACK response");
        return PICOMSO_SIGROK_ERR_PROTOCOL;
    }

    return PICOMSO_SIGROK_OK;
}

static picomso_sigrok_status_t get_status_response(picomso_sigrok_driver_t *driver,
                                                   picomso_status_response_t *status_response)
{
    picomso_sigrok_response_t response;
    picomso_sigrok_device_error_t device_error;
    picomso_sigrok_status_t status;

    status = transport_round_trip(driver, PICOMSO_MSG_GET_STATUS, NULL, 0u, &response);
    if (status != PICOMSO_SIGROK_OK) {
        return status;
    }

    if (response.header.msg_type == PICOMSO_MSG_ERROR) {
        if (!picomso_sigrok_decode_device_error(&response, &device_error, driver->last_error,
                                                sizeof(driver->last_error))) {
            set_error(driver, "failed to decode PicoMSO GET_STATUS error");
            return PICOMSO_SIGROK_ERR_PROTOCOL;
        }
        return PICOMSO_SIGROK_ERR_DEVICE;
    }

    if (!picomso_sigrok_decode_status(&response, status_response, driver->last_error,
                                      sizeof(driver->last_error))) {
        return PICOMSO_SIGROK_ERR_PROTOCOL;
    }

    return PICOMSO_SIGROK_OK;
}

static picomso_sigrok_status_t select_logic_mode(picomso_sigrok_driver_t *driver)
{
    picomso_set_mode_request_t request;

    request.mode = PICOMSO_MODE_LOGIC;
    return expect_ack_only(driver, PICOMSO_MSG_SET_MODE, &request, sizeof(request));
}

void picomso_sigrok_driver_init(picomso_sigrok_driver_t *driver,
                                const picomso_sigrok_transport_ops_t *transport_ops,
                                void *transport_data)
{
    if (driver == NULL) {
        return;
    }

    memset(driver, 0, sizeof(*driver));
    driver->transport_ops = transport_ops;
    driver->transport_data = transport_data;
    driver->next_seq = 1u;
}

picomso_sigrok_status_t picomso_sigrok_dev_open(picomso_sigrok_driver_t *driver)
{
    picomso_sigrok_response_t response;
    picomso_sigrok_device_error_t device_error;
    picomso_info_response_t info;
    picomso_capabilities_response_t capabilities;
    picomso_status_response_t status_response;
    picomso_sigrok_status_t status;
    size_t firmware_id_copy_len;

    if (driver == NULL || driver->transport_ops == NULL) {
        return PICOMSO_SIGROK_ERR_ARG;
    }

    driver->last_error[0] = '\0';

    status = transport_round_trip(driver, PICOMSO_MSG_GET_INFO, NULL, 0u, &response);
    if (status != PICOMSO_SIGROK_OK) {
        return status;
    }
    if (response.header.msg_type == PICOMSO_MSG_ERROR) {
        if (!picomso_sigrok_decode_device_error(&response, &device_error, driver->last_error,
                                                sizeof(driver->last_error))) {
            set_error(driver, "failed to decode PicoMSO GET_INFO error");
            return PICOMSO_SIGROK_ERR_PROTOCOL;
        }
        return PICOMSO_SIGROK_ERR_DEVICE;
    }
    if (!picomso_sigrok_decode_info(&response, &info, driver->last_error, sizeof(driver->last_error))) {
        return PICOMSO_SIGROK_ERR_PROTOCOL;
    }

    driver->protocol_version_major = info.protocol_version_major;
    driver->protocol_version_minor = info.protocol_version_minor;
    firmware_id_copy_len = (sizeof(driver->firmware_id) < sizeof(info.fw_id))
                               ? sizeof(driver->firmware_id)
                               : sizeof(info.fw_id);
    memcpy(driver->firmware_id, info.fw_id, firmware_id_copy_len);
    driver->firmware_id[sizeof(driver->firmware_id) - 1u] = '\0';

    status = transport_round_trip(driver, PICOMSO_MSG_GET_CAPABILITIES, NULL, 0u, &response);
    if (status != PICOMSO_SIGROK_OK) {
        return status;
    }
    if (response.header.msg_type == PICOMSO_MSG_ERROR) {
        if (!picomso_sigrok_decode_device_error(&response, &device_error, driver->last_error,
                                                sizeof(driver->last_error))) {
            set_error(driver, "failed to decode PicoMSO GET_CAPABILITIES error");
            return PICOMSO_SIGROK_ERR_PROTOCOL;
        }
        return PICOMSO_SIGROK_ERR_DEVICE;
    }
    if (!picomso_sigrok_decode_capabilities(&response, &capabilities, driver->last_error,
                                            sizeof(driver->last_error))) {
        return PICOMSO_SIGROK_ERR_PROTOCOL;
    }

    driver->capabilities = capabilities.capabilities;
    if ((driver->capabilities & PICOMSO_CAP_LOGIC) == 0u) {
        set_error(driver, "device does not advertise PicoMSO logic capability");
        return PICOMSO_SIGROK_ERR_UNSUPPORTED;
    }

    status = select_logic_mode(driver);
    if (status != PICOMSO_SIGROK_OK) {
        return status;
    }

    status = get_status_response(driver, &status_response);
    if (status != PICOMSO_SIGROK_OK) {
        return status;
    }
    if (status_response.mode != PICOMSO_MODE_LOGIC) {
        set_error(driver, "device did not enter PicoMSO logic mode");
        return PICOMSO_SIGROK_ERR_PROTOCOL;
    }

    driver->next_block_id = 0u;
    driver->is_open = true;
    driver->acquisition_running = false;
    return PICOMSO_SIGROK_OK;
}

picomso_sigrok_status_t picomso_sigrok_dev_close(picomso_sigrok_driver_t *driver)
{
    if (driver == NULL) {
        return PICOMSO_SIGROK_ERR_ARG;
    }

    if (driver->is_open && driver->acquisition_running) {
        picomso_sigrok_status_t status = picomso_sigrok_acquisition_stop(driver);
        if (status != PICOMSO_SIGROK_OK) {
            return status;
        }
    }

    driver->is_open = false;
    driver->acquisition_running = false;
    return PICOMSO_SIGROK_OK;
}

picomso_sigrok_status_t picomso_sigrok_acquisition_start(picomso_sigrok_driver_t *driver,
                                                         const picomso_sigrok_logic_config_t *config,
                                                         picomso_sigrok_samples_cb_t samples_cb,
                                                         void *samples_cb_data,
                                                         size_t *captured_samples_out)
{
    picomso_request_capture_request_t request;
    picomso_status_response_t status_response;
    picomso_sigrok_response_t response;
    picomso_sigrok_device_error_t device_error;
    picomso_sigrok_status_t status;
    uint32_t waited_ms = 0u;
    size_t captured_samples = 0u;

    if (driver == NULL || config == NULL || samples_cb == NULL) {
        return PICOMSO_SIGROK_ERR_ARG;
    }

    if (!driver->is_open) {
        set_error(driver, "device is not open");
        return PICOMSO_SIGROK_ERR_ARG;
    }

    memset(&request, 0, sizeof(request));
    request.total_samples = config->total_samples;
    request.rate = config->sample_rate_hz;
    request.pre_trigger_samples = config->pre_trigger_samples;
    memcpy(request.trigger, config->triggers, sizeof(request.trigger));

    driver->next_block_id = 0u;
    status = expect_ack_only(driver, PICOMSO_MSG_REQUEST_CAPTURE, &request, sizeof(request));
    if (status != PICOMSO_SIGROK_OK) {
        return status;
    }

    driver->acquisition_running = true;
    for (;;) {
        status = get_status_response(driver, &status_response);
        if (status != PICOMSO_SIGROK_OK) {
            goto fail;
        }

        if (status_response.mode != PICOMSO_MODE_LOGIC) {
            set_error(driver, "device left PicoMSO logic mode during acquisition");
            status = PICOMSO_SIGROK_ERR_PROTOCOL;
            goto fail;
        }

        if (status_response.capture_state == PICOMSO_CAPTURE_IDLE) {
            break;
        }

        if (waited_ms >= config->poll_timeout_ms) {
            set_error(driver, "timed out waiting for PicoMSO capture to complete");
            status = PICOMSO_SIGROK_ERR_TIMEOUT;
            goto fail;
        }

        if (driver->transport_ops->sleep_ms != NULL && config->poll_interval_ms > 0u) {
            driver->transport_ops->sleep_ms(driver->transport_data, config->poll_interval_ms);
        }
        waited_ms += config->poll_interval_ms;
    }

    for (;;) {
        uint16_t block_id;
        uint16_t data_len;
        const uint8_t *data;

        status = transport_round_trip(driver, PICOMSO_MSG_READ_DATA_BLOCK, NULL, 0u, &response);
        if (status != PICOMSO_SIGROK_OK) {
            goto fail;
        }

        if (response.header.msg_type == PICOMSO_MSG_ERROR) {
            if (!picomso_sigrok_decode_device_error(&response, &device_error, driver->last_error,
                                                    sizeof(driver->last_error))) {
                set_error(driver, "failed to decode PicoMSO READ_DATA_BLOCK error");
                status = PICOMSO_SIGROK_ERR_PROTOCOL;
                goto fail;
            }
            if (picomso_sigrok_device_error_is_end_of_capture(&device_error)) {
                break;
            }
            status = PICOMSO_SIGROK_ERR_DEVICE;
            goto fail;
        }

        if (!picomso_sigrok_decode_data_block(&response, &block_id, &data, &data_len,
                                              driver->last_error, sizeof(driver->last_error))) {
            status = PICOMSO_SIGROK_ERR_PROTOCOL;
            goto fail;
        }

        if (block_id != driver->next_block_id) {
            set_error(driver, "unexpected PicoMSO block id");
            status = PICOMSO_SIGROK_ERR_PROTOCOL;
            goto fail;
        }
        driver->next_block_id++;

        status = forward_logic_samples(driver, samples_cb, samples_cb_data, data, data_len,
                                       &captured_samples);
        if (status != PICOMSO_SIGROK_OK) {
            goto fail;
        }
    }

    driver->acquisition_running = false;
    if (captured_samples_out != NULL) {
        *captured_samples_out = captured_samples;
    }
    return PICOMSO_SIGROK_OK;

fail:
    driver->acquisition_running = false;
    return status;
}

picomso_sigrok_status_t picomso_sigrok_acquisition_stop(picomso_sigrok_driver_t *driver)
{
    picomso_sigrok_status_t status;

    if (driver == NULL) {
        return PICOMSO_SIGROK_ERR_ARG;
    }

    if (!driver->is_open) {
        return PICOMSO_SIGROK_OK;
    }

    status = select_logic_mode(driver);
    if (status != PICOMSO_SIGROK_OK) {
        return status;
    }

    driver->acquisition_running = false;
    driver->next_block_id = 0u;
    return PICOMSO_SIGROK_OK;
}

const char *picomso_sigrok_last_error(const picomso_sigrok_driver_t *driver)
{
    if (driver == NULL || driver->last_error[0] == '\0') {
        return "";
    }

    return driver->last_error;
}
