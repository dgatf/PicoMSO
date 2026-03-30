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
 * PicoMSO unified protocol – transport-agnostic skeleton (Phase 0).
 *
 * This header defines the wire format, message-type enumeration, and
 * request/response dispatch API for the future PicoMSO host protocol.
 *
 * Intentional constraints for Phase 0:
 *   - No dependency on USB, CDC, bulk endpoints, PIO, ADC, or DMA.
 *   - No dependency on any specific transport framing layer.
 *   - Callers supply raw byte buffers; the layer parses and dispatches.
 *   - The initial commands handled are GET_INFO, GET_CAPABILITIES,
 *     GET_STATUS, SET_MODE, REQUEST_CAPTURE, and READ_DATA_BLOCK.
 *   - The existing SUMP and oscilloscope protocols are NOT replaced.
 */

#ifndef PICOMSO_PROTOCOL_H
#define PICOMSO_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Version constants
 * ----------------------------------------------------------------------- */

/** Protocol major version.  Bump when wire format changes incompatibly. */
#define PICOMSO_PROTOCOL_VERSION_MAJOR  0

/** Protocol minor version.  Bump when new commands are added. */
#define PICOMSO_PROTOCOL_VERSION_MINOR  3

/* -----------------------------------------------------------------------
 * Packet framing constants
 * ----------------------------------------------------------------------- */

/**
 * Two-byte magic value placed at the start of every packet ("MS" in ASCII,
 * little-endian: 0x53 0x4D on the wire).
 */
#define PICOMSO_PACKET_MAGIC  UINT16_C(0x4D53)

/** Minimum number of bytes a valid packet must supply. */
#define PICOMSO_PACKET_HEADER_SIZE  ((size_t)sizeof(picomso_packet_header_t))

/** Maximum payload length accepted by this implementation (bytes). */
#define PICOMSO_MAX_PAYLOAD_LEN  UINT16_C(512)

/* -----------------------------------------------------------------------
 * Packet header
 *
 * All multi-byte fields are little-endian.
 *
 * Offset  Size  Field
 *   0      2    magic        – must equal PICOMSO_PACKET_MAGIC
 *   2      1    version_major
 *   3      1    version_minor
 *   4      1    msg_type     – picomso_msg_type_t
 *   5      1    seq          – sequence number (echoed in response)
 *   6      2    length       – byte count of payload that follows the header
 * ----------------------------------------------------------------------- */

typedef struct {
    uint16_t magic;
    uint8_t  version_major;
    uint8_t  version_minor;
    uint8_t  msg_type;
    uint8_t  seq;
    uint16_t length;
} __attribute__((packed)) picomso_packet_header_t;

/* -----------------------------------------------------------------------
 * Message type enumeration
 * ----------------------------------------------------------------------- */

typedef enum {
    /* Requests (host → device) */
    PICOMSO_MSG_GET_INFO          = 0x01,
    PICOMSO_MSG_GET_CAPABILITIES  = 0x02,
    PICOMSO_MSG_GET_STATUS        = 0x03,
    PICOMSO_MSG_SET_MODE          = 0x04,
    PICOMSO_MSG_REQUEST_CAPTURE   = 0x05, /**< Perform one full one-shot capture request */
    PICOMSO_MSG_READ_DATA_BLOCK   = 0x06, /**< Read one chunk from the finalized capture buffer */

    /* Responses (device → host) */
    PICOMSO_MSG_ACK               = 0x80,
    PICOMSO_MSG_ERROR             = 0x81,
    PICOMSO_MSG_DATA_BLOCK        = 0x82, /**< Data-plane response carrying sample bytes */
} picomso_msg_type_t;

/* -----------------------------------------------------------------------
 * Status / error codes
 * ----------------------------------------------------------------------- */

typedef enum {
    PICOMSO_STATUS_OK             = 0x00,
    PICOMSO_STATUS_ERR_UNKNOWN    = 0x01, /**< Unrecognised command */
    PICOMSO_STATUS_ERR_BAD_MAGIC  = 0x02, /**< Magic bytes mismatch */
    PICOMSO_STATUS_ERR_BAD_LEN    = 0x03, /**< Payload length out of range */
    PICOMSO_STATUS_ERR_BAD_MODE   = 0x04, /**< Unknown mode in SET_MODE */
    PICOMSO_STATUS_ERR_VERSION    = 0x05, /**< Incompatible protocol version */
} picomso_status_t;

/* -----------------------------------------------------------------------
 * ACK packet (device → host, on success)
 *
 * Header msg_type = PICOMSO_MSG_ACK
 * Payload:
 *   Offset  Size  Field
 *     0      1    status   – PICOMSO_STATUS_OK
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t status;
} __attribute__((packed)) picomso_ack_payload_t;

/* -----------------------------------------------------------------------
 * ERROR packet (device → host, on failure)
 *
 * Header msg_type = PICOMSO_MSG_ERROR
 * Payload:
 *   Offset  Size  Field
 *     0      1    status      – picomso_status_t error code
 *     1      1    msg_len     – byte length of the human-readable message
 *     2    msg_len  message   – UTF-8 string (no NUL terminator on wire)
 * ----------------------------------------------------------------------- */

typedef struct {
    uint8_t status;
    uint8_t msg_len;
    /* Followed by msg_len bytes of human-readable text. */
} __attribute__((packed)) picomso_error_payload_t;

/* -----------------------------------------------------------------------
 * Response buffer
 *
 * Callers pass a picomso_response_t into picomso_dispatch().  On return
 * 'used' contains the number of bytes written into 'buf'.  The buffer
 * includes the full response packet (header + payload).
 *
 * The buffer size is chosen to be large enough for any response produced
 * by this implementation.
 * ----------------------------------------------------------------------- */

/** Size in bytes of each logic-capture payload block returned by READ_DATA_BLOCK. */
#define PICOMSO_DATA_BLOCK_SIZE  64u

/** Response buffer large enough for any packet this implementation produces. */
#define PICOMSO_RESPONSE_BUF_SIZE  256u

typedef struct {
    uint8_t buf[PICOMSO_RESPONSE_BUF_SIZE];
    size_t  used;
} picomso_response_t;

/* -----------------------------------------------------------------------
 * Dispatch API
 * ----------------------------------------------------------------------- */

/**
 * Validate and dispatch one incoming packet.
 *
 * @param in_buf   Pointer to the raw received bytes (header + payload).
 * @param in_len   Total byte count of in_buf.
 * @param resp     Output buffer.  On success or recognised-error, a full
 *                 response packet is written here and resp->used is set.
 *                 On catastrophic framing failure resp->used is set to 0.
 * @return PICOMSO_STATUS_OK if the packet was handled without error,
 *         or a picomso_status_t error code otherwise.
 */
picomso_status_t picomso_dispatch(const uint8_t      *in_buf,
                                  size_t              in_len,
                                  picomso_response_t *resp);

/**
 * Write a complete ACK response packet into resp.
 *
 * @param seq   Sequence number copied from the incoming request header.
 * @param resp  Output buffer to write into.
 */
void picomso_write_ack(uint8_t seq, picomso_response_t *resp);

/**
 * Write a complete ERROR response packet into resp.
 *
 * @param seq     Sequence number copied from the incoming request header.
 * @param status  Error code (must not be PICOMSO_STATUS_OK).
 * @param msg     Human-readable NUL-terminated string (may be NULL).
 * @param resp    Output buffer to write into.
 */
void picomso_write_error(uint8_t            seq,
                         picomso_status_t   status,
                         const char        *msg,
                         picomso_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* PICOMSO_PROTOCOL_H */
