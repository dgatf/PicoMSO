#ifndef PICOMSO_PROTOCOL_PACKETS_H
#define PICOMSO_PROTOCOL_PACKETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "protocol.h"

#define PICOMSO_DATA_BLOCK_FLAG_LOGIC_FINALIZED  UINT8_C(1u << 0)
#define PICOMSO_DATA_BLOCK_FLAG_SCOPE_FINALIZED  UINT8_C(1u << 1)
#define PICOMSO_DATA_BLOCK_FLAG_TERMINAL         UINT8_C(1u << 2)

/* -----------------------------------------------------------------------
 * STREAM DEFINITIONS
 * ----------------------------------------------------------------------- */

#define PICOMSO_STREAM_NONE UINT8_C(0)
#define PICOMSO_STREAM_LOGIC UINT8_C(1 << 0)
#define PICOMSO_STREAM_SCOPE UINT8_C(1 << 1)

/* -----------------------------------------------------------------------
 * GET_INFO
 * ----------------------------------------------------------------------- */

#define PICOMSO_INFO_FW_ID_MAX 32u

typedef struct {
    uint8_t protocol_version_major;
    uint8_t protocol_version_minor;
    char fw_id[PICOMSO_INFO_FW_ID_MAX];
} __attribute__((packed)) picomso_info_response_t;

/* -----------------------------------------------------------------------
 * GET_CAPABILITIES
 * ----------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    uint8_t version;                 // = 1
    uint8_t size;                    // sizeof(picomso_capabilities_v1_t)

    uint8_t capabilities_flags;      // bitmask of PICOMSO_CAP_* flags (reserved for future use; currently always 0)
    uint8_t max_logic_channels;      // e.g. 16
    uint8_t max_analog_channels;     // e.g. 1

    uint32_t max_samplerate_logic;   // Hz
    uint32_t max_samplerate_scope;   // Hz

    uint32_t max_samples_logic;      // samples
    uint32_t max_samples_scope;      // samples
} picomso_capabilities_t;

/* -----------------------------------------------------------------------
 * GET_STATUS
 * ----------------------------------------------------------------------- */

typedef enum {
    PICOMSO_CAPTURE_IDLE = 0x00,
    PICOMSO_CAPTURE_RUNNING = 0x01,
} picomso_capture_state_t;

typedef struct {
    uint8_t streams;       /**< Bitmask of PICOMSO_STREAM_* */
    uint8_t capture_state; /**< picomso_capture_state_t     */
} __attribute__((packed)) picomso_status_response_t;

/* -----------------------------------------------------------------------
 * SET_MODE (now SET_STREAMS conceptually)
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t streams; /**< Bitmask of PICOMSO_STREAM_* */
} __attribute__((packed)) picomso_set_mode_request_t;

/* -----------------------------------------------------------------------
 * REQUEST_CAPTURE
 * ----------------------------------------------------------------------- */

#define PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT 4u

typedef enum {
    PICOMSO_TRIGGER_MATCH_LEVEL_LOW = 0x00,
    PICOMSO_TRIGGER_MATCH_LEVEL_HIGH = 0x01,
    PICOMSO_TRIGGER_MATCH_EDGE_LOW = 0x02,
    PICOMSO_TRIGGER_MATCH_EDGE_HIGH = 0x03,
} picomso_trigger_match_t;

typedef struct {
    uint8_t is_enabled;
    uint8_t pin;
    uint8_t match;
} __attribute__((packed)) picomso_trigger_config_t;

/**
 * analog_channels is a bitmask of ADC inputs to enable for scope capture.
 * Bit 0 = ADC input 0 (GPIO 26), bit 1 = ADC input 1 (GPIO 27),
 * bit 2 = ADC input 2 (GPIO 28).  Only bits 0-2 are valid.
 * A value of 0x00 is treated as 0x01 (ADC input 0 only) for backward
 * compatibility.  The firmware round-robins the selected inputs in
 * ascending index order; total_samples is the total interleaved count
 * across all selected channels.
 *
 * Hosts that do not set this field may send the 24-byte form of the
 * packet (without this trailing byte); the firmware then defaults to
 * ADC input 0 only.
 */
typedef struct {
    uint32_t total_samples;
    uint32_t rate;
    uint32_t pre_trigger_samples;
    picomso_trigger_config_t trigger[PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT];
    uint8_t analog_channels;
} __attribute__((packed)) picomso_request_capture_request_t;

/* -----------------------------------------------------------------------
 * READ_DATA_BLOCK / DATA_BLOCK
 * ----------------------------------------------------------------------- */

/*
 * New payload layout (mixed-ready):
 *
 * Offset   Size    Field
 *   0        1     stream_id
 *   1        1     flags
 *   2        2     block_id
 *   4        2     data_len
 *   6      data_len data[]
 */

typedef enum {
    PICOMSO_STREAM_ID_LOGIC = 1,
    PICOMSO_STREAM_ID_SCOPE = 2,
} picomso_stream_id_t;

typedef struct {
    uint8_t stream_id; /**< picomso_stream_id_t */
    uint8_t flags;
    uint16_t block_id; /**< 16-bit as agreed */
    uint16_t data_len;
    uint8_t data[PICOMSO_DATA_BLOCK_SIZE];
} __attribute__((packed)) picomso_data_block_response_t;

#ifdef __cplusplus
}
#endif

#endif