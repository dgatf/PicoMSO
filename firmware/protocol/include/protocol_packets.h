#ifndef PICOMSO_PROTOCOL_PACKETS_H
#define PICOMSO_PROTOCOL_PACKETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "protocol.h"

/* -----------------------------------------------------------------------
 * STREAM DEFINITIONS
 * ----------------------------------------------------------------------- */

#define PICOMSO_STREAM_NONE   UINT8_C(0)
#define PICOMSO_STREAM_LOGIC  UINT8_C(1 << 0)
#define PICOMSO_STREAM_SCOPE  UINT8_C(1 << 1)

/* -----------------------------------------------------------------------
 * GET_INFO
 * ----------------------------------------------------------------------- */

#define PICOMSO_INFO_FW_ID_MAX  32u

typedef struct {
    uint8_t protocol_version_major;
    uint8_t protocol_version_minor;
    char fw_id[PICOMSO_INFO_FW_ID_MAX];
} __attribute__((packed)) picomso_info_response_t;

/* -----------------------------------------------------------------------
 * GET_CAPABILITIES
 * ----------------------------------------------------------------------- */

#define PICOMSO_CAP_LOGIC  UINT32_C(1 << 0)
#define PICOMSO_CAP_SCOPE  UINT32_C(1 << 1)

typedef struct {
    uint32_t capabilities;
} __attribute__((packed)) picomso_capabilities_response_t;

/* -----------------------------------------------------------------------
 * GET_STATUS
 * ----------------------------------------------------------------------- */

typedef enum {
    PICOMSO_CAPTURE_IDLE    = 0x00,
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

#define PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT  4u

typedef enum {
    PICOMSO_TRIGGER_MATCH_LEVEL_LOW  = 0x00,
    PICOMSO_TRIGGER_MATCH_LEVEL_HIGH = 0x01,
    PICOMSO_TRIGGER_MATCH_EDGE_LOW   = 0x02,
    PICOMSO_TRIGGER_MATCH_EDGE_HIGH  = 0x03,
} picomso_trigger_match_t;

typedef struct {
    uint8_t is_enabled;
    uint8_t pin;
    uint8_t match;
} __attribute__((packed)) picomso_trigger_config_t;

typedef struct {
    uint32_t total_samples;
    uint32_t rate;
    uint32_t pre_trigger_samples;
    picomso_trigger_config_t trigger[PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT];
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
    uint8_t  stream_id; /**< picomso_stream_id_t */
    uint8_t  flags;
    uint16_t block_id;  /**< 16-bit as agreed */
    uint16_t data_len;
    uint8_t  data[PICOMSO_DATA_BLOCK_SIZE];
} __attribute__((packed)) picomso_data_block_response_t;

#ifdef __cplusplus
}
#endif

#endif