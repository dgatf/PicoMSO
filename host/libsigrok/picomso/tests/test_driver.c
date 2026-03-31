#include "protocol.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RESPONSE_BUFFER_SIZE 256u
#define MAX_RESPONSES 16u
#define MAX_WRITES 16u
#define MAX_WRITE_SIZE 64u
#define MAX_CAPTURED_SAMPLES 16u

typedef struct {
    uint8_t data[RESPONSE_BUFFER_SIZE];
    size_t length;
} scripted_response_t;

typedef struct {
    scripted_response_t responses[MAX_RESPONSES];
    size_t response_count;
    size_t next_response;
    uint8_t writes[MAX_WRITES][MAX_WRITE_SIZE];
    size_t write_lengths[MAX_WRITES];
    size_t write_count;
    unsigned int wait_calls;
} mock_transport_t;

typedef struct {
    uint16_t samples[MAX_CAPTURED_SAMPLES];
    size_t count;
} capture_sink_t;

typedef int (*picomso_logic_samples_cb)(void *user_data, const uint16_t *samples, size_t sample_count);

typedef struct {
    picomso_protocol_t protocol;
    picomso_info_response_t info;
    uint32_t capabilities;
    unsigned int channel_count;
    bool is_open;
    bool logic_mode_active;
    bool capture_running;
} picomso_driver_t;

void picomso_driver_init(picomso_driver_t *driver, const picomso_transport_t *transport);
picomso_result_t picomso_driver_open(picomso_driver_t *driver);
picomso_result_t picomso_driver_close(picomso_driver_t *driver);
picomso_result_t picomso_driver_start_logic_capture(picomso_driver_t *driver,
                                                    const picomso_request_capture_request_t *request);
picomso_result_t picomso_driver_wait_capture_complete(picomso_driver_t *driver,
                                                      unsigned int max_polls,
                                                      unsigned int poll_interval_ms);
picomso_result_t picomso_driver_read_logic_capture(picomso_driver_t *driver,
                                                   picomso_logic_samples_cb callback,
                                                   void *user_data,
                                                   size_t *captured_samples);
const char *picomso_driver_logic_channel_name(unsigned int index);

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

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void add_response(mock_transport_t *mock,
                         uint8_t seq,
                         uint8_t msg_type,
                         const uint8_t *payload,
                         uint16_t payload_len)
{
    scripted_response_t *response = &mock->responses[mock->response_count++];

    write_u16_le(response->data, PICOMSO_PACKET_MAGIC);
    response->data[2] = PICOMSO_PROTOCOL_VERSION_MAJOR;
    response->data[3] = PICOMSO_PROTOCOL_VERSION_MINOR;
    response->data[4] = msg_type;
    response->data[5] = seq;
    write_u16_le(response->data + 6, payload_len);
    if (payload_len > 0u) {
        memcpy(response->data + PICOMSO_PACKET_HEADER_SIZE, payload, payload_len);
    }
    response->length = PICOMSO_PACKET_HEADER_SIZE + payload_len;
}

static void add_info_response(mock_transport_t *mock, uint8_t seq)
{
    uint8_t payload[sizeof(picomso_info_response_t)] = {0};
    payload[0] = PICOMSO_PROTOCOL_VERSION_MAJOR;
    payload[1] = PICOMSO_PROTOCOL_VERSION_MINOR;
    memcpy(payload + 2, "PicoMSO-0.1", strlen("PicoMSO-0.1"));
    add_response(mock, seq, PICOMSO_MSG_ACK, payload, sizeof(payload));
}

static void add_capabilities_response(mock_transport_t *mock, uint8_t seq, uint32_t capabilities)
{
    uint8_t payload[4];
    write_u32_le(payload, capabilities);
    add_response(mock, seq, PICOMSO_MSG_ACK, payload, sizeof(payload));
}

static void add_status_response(mock_transport_t *mock, uint8_t seq, uint8_t mode, uint8_t capture_state)
{
    uint8_t payload[2] = {mode, capture_state};
    add_response(mock, seq, PICOMSO_MSG_ACK, payload, sizeof(payload));
}

static void add_ack_response(mock_transport_t *mock, uint8_t seq)
{
    uint8_t payload[1] = {(uint8_t)PICOMSO_STATUS_OK};
    add_response(mock, seq, PICOMSO_MSG_ACK, payload, sizeof(payload));
}

static void add_error_response(mock_transport_t *mock, uint8_t seq, uint8_t status, const char *message)
{
    uint8_t payload[2 + PICOMSO_PROTOCOL_ERROR_TEXT_MAX] = {0};
    size_t message_len = strlen(message);

    payload[0] = status;
    payload[1] = (uint8_t)message_len;
    memcpy(payload + 2, message, message_len);
    add_response(mock, seq, PICOMSO_MSG_ERROR, payload, (uint16_t)(2u + message_len));
}

static void add_data_block_response(mock_transport_t *mock, uint8_t seq, uint16_t block_id, const uint8_t *data, uint16_t data_len)
{
    uint8_t payload[sizeof(picomso_data_block_response_t)] = {0};

    write_u16_le(payload, block_id);
    write_u16_le(payload + 2, data_len);
    if (data_len > 0u) {
        memcpy(payload + 4, data, data_len);
    }
    add_response(mock, seq, PICOMSO_MSG_DATA_BLOCK, payload, sizeof(payload));
}

static int mock_control_write(void *user_data, const uint8_t *data, size_t length)
{
    mock_transport_t *mock = (mock_transport_t *)user_data;

    assert(mock->write_count < MAX_WRITES);
    assert(length <= MAX_WRITE_SIZE);
    memcpy(mock->writes[mock->write_count], data, length);
    mock->write_lengths[mock->write_count] = length;
    mock->write_count++;
    return 0;
}

static int mock_bulk_read(void *user_data, uint8_t *data, size_t capacity, size_t *actual_length)
{
    mock_transport_t *mock = (mock_transport_t *)user_data;
    scripted_response_t *response;

    assert(mock->next_response < mock->response_count);
    response = &mock->responses[mock->next_response++];
    assert(response->length <= capacity);
    memcpy(data, response->data, response->length);
    *actual_length = response->length;
    return 0;
}

static int mock_wait_ms(void *user_data, unsigned int delay_ms)
{
    mock_transport_t *mock = (mock_transport_t *)user_data;

    (void)delay_ms;
    mock->wait_calls++;
    return 0;
}

static int capture_samples(void *user_data, const uint16_t *samples, size_t sample_count)
{
    capture_sink_t *sink = (capture_sink_t *)user_data;
    size_t i;

    assert(sink->count + sample_count <= MAX_CAPTURED_SAMPLES);
    for (i = 0u; i < sample_count; ++i) {
        sink->samples[sink->count++] = samples[i];
    }
    return 0;
}

static void test_logic_capture_flow_accepts_variable_length_result(void)
{
    mock_transport_t mock = {0};
    capture_sink_t sink = {0};
    picomso_transport_t transport = {
        .control_write = mock_control_write,
        .bulk_read = mock_bulk_read,
        .wait_ms = mock_wait_ms,
        .user_data = &mock,
    };
    picomso_driver_t driver;
    picomso_request_capture_request_t request = {0};
    uint8_t first_block[] = {0x01, 0x00, 0x02, 0x00};
    uint8_t second_block[] = {0x03, 0x00};
    size_t captured_samples = 0u;
    picomso_result_t result;

    add_info_response(&mock, 1u);
    add_capabilities_response(&mock, 2u, PICOMSO_CAP_LOGIC | PICOMSO_CAP_SCOPE);
    add_ack_response(&mock, 3u);
    add_status_response(&mock, 4u, PICOMSO_MODE_LOGIC, PICOMSO_CAPTURE_IDLE);
    add_ack_response(&mock, 5u);
    add_status_response(&mock, 6u, PICOMSO_MODE_LOGIC, PICOMSO_CAPTURE_RUNNING);
    add_status_response(&mock, 7u, PICOMSO_MODE_LOGIC, PICOMSO_CAPTURE_IDLE);
    add_data_block_response(&mock, 8u, 0u, first_block, sizeof(first_block));
    add_data_block_response(&mock, 9u, 1u, second_block, sizeof(second_block));
    add_error_response(&mock, 10u, PICOMSO_STATUS_ERR_UNKNOWN, "no finalized capture data");
    add_ack_response(&mock, 11u);

    request.total_samples = 5u;
    request.rate = 100000u;
    request.pre_trigger_samples = 4u;
    request.trigger[0].is_enabled = 1u;
    request.trigger[0].pin = 3u;
    request.trigger[0].match = PICOMSO_TRIGGER_MATCH_EDGE_HIGH;

    picomso_driver_init(&driver, &transport);
    assert(strcmp(picomso_driver_logic_channel_name(15u), "D15") == 0);
    assert(picomso_driver_logic_channel_name(16u) == NULL);

    result = picomso_driver_open(&driver);
    assert(result == PICOMSO_RESULT_OK);
    assert(driver.channel_count == PICOMSO_DRIVER_CHANNEL_COUNT);

    result = picomso_driver_start_logic_capture(&driver, &request);
    assert(result == PICOMSO_RESULT_OK);

    result = picomso_driver_wait_capture_complete(&driver, 4u, 1u);
    assert(result == PICOMSO_RESULT_OK);
    assert(mock.wait_calls == 1u);

    result = picomso_driver_read_logic_capture(&driver, capture_samples, &sink, &captured_samples);
    assert(result == PICOMSO_RESULT_OK);
    assert(captured_samples == 3u);
    assert(sink.count == 3u);
    assert(sink.samples[0] == 1u);
    assert(sink.samples[1] == 2u);
    assert(sink.samples[2] == 3u);

    result = picomso_driver_close(&driver);
    assert(result == PICOMSO_RESULT_OK);

    assert(mock.write_count == 11u);
    assert(mock.writes[0][4] == PICOMSO_MSG_GET_INFO);
    assert(mock.writes[1][4] == PICOMSO_MSG_GET_CAPABILITIES);
    assert(mock.writes[2][4] == PICOMSO_MSG_SET_MODE);
    assert(mock.writes[3][4] == PICOMSO_MSG_GET_STATUS);
    assert(mock.writes[4][4] == PICOMSO_MSG_REQUEST_CAPTURE);
    assert(mock.writes[5][4] == PICOMSO_MSG_GET_STATUS);
    assert(mock.writes[6][4] == PICOMSO_MSG_GET_STATUS);
    assert(mock.writes[7][4] == PICOMSO_MSG_READ_DATA_BLOCK);
    assert(mock.writes[8][4] == PICOMSO_MSG_READ_DATA_BLOCK);
    assert(mock.writes[9][4] == PICOMSO_MSG_READ_DATA_BLOCK);
    assert(mock.writes[10][4] == PICOMSO_MSG_SET_MODE);

    assert(read_u32_le(mock.writes[4] + PICOMSO_PACKET_HEADER_SIZE) == request.total_samples);
    assert(read_u32_le(mock.writes[4] + PICOMSO_PACKET_HEADER_SIZE + 4u) == request.rate);
    assert(read_u32_le(mock.writes[4] + PICOMSO_PACKET_HEADER_SIZE + 8u) == request.pre_trigger_samples);
    assert(mock.writes[4][PICOMSO_PACKET_HEADER_SIZE + 12u] == 1u);
    assert(mock.writes[4][PICOMSO_PACKET_HEADER_SIZE + 13u] == 3u);
    assert(mock.writes[4][PICOMSO_PACKET_HEADER_SIZE + 14u] == PICOMSO_TRIGGER_MATCH_EDGE_HIGH);
    assert(mock.writes[10][PICOMSO_PACKET_HEADER_SIZE] == PICOMSO_MODE_UNSET);
}

static void test_open_requires_logic_capability(void)
{
    mock_transport_t mock = {0};
    picomso_transport_t transport = {
        .control_write = mock_control_write,
        .bulk_read = mock_bulk_read,
        .wait_ms = mock_wait_ms,
        .user_data = &mock,
    };
    picomso_driver_t driver;
    picomso_result_t result;

    add_info_response(&mock, 1u);
    add_capabilities_response(&mock, 2u, PICOMSO_CAP_SCOPE);

    picomso_driver_init(&driver, &transport);
    result = picomso_driver_open(&driver);
    assert(result == PICOMSO_RESULT_ERR_UNSUPPORTED);
    assert(mock.write_count == 2u);
}

int main(void)
{
    test_logic_capture_flow_accepts_variable_length_result();
    test_open_requires_logic_capability();
    puts("picomso_sigrok_driver_test: all tests passed");
    return 0;
}
