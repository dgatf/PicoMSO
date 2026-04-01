#ifndef CAPTURE_CONTROLLER_H
#define CAPTURE_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "types.h"

/*
 * Stream bitmask values used by the control layer.
 *
 * These values intentionally match the protocol layer stream bitmask,
 * but the capture controller keeps this definition local so that the
 * common module does not depend on protocol headers.
 */
#define PICOMSO_STREAM_NONE   UINT8_C(0)
#define PICOMSO_STREAM_LOGIC  UINT8_C(1 << 0)
#define PICOMSO_STREAM_SCOPE  UINT8_C(1 << 1)

typedef struct {
    uint8_t streams_enabled;
    capture_state_t state;
} capture_controller_t;

void capture_controller_init(capture_controller_t *ctrl);
void capture_controller_set_streams(capture_controller_t *ctrl, uint8_t streams);
void capture_controller_set_state(capture_controller_t *ctrl, capture_state_t state);
uint8_t capture_controller_get_streams(const capture_controller_t *ctrl);
capture_state_t capture_controller_get_state(const capture_controller_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif