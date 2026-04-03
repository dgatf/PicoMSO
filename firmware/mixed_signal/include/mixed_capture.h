#ifndef MIXED_CAPTURE_H
#define MIXED_CAPTURE_H

#include <stdbool.h>

#include "types.h"

bool mixed_capture_start(const capture_config_t *logic_config, const capture_config_t *scope_config,
                         complete_handler_t logic_handler, complete_handler_t scope_handler);

void mixed_capture_reset(void);

#endif