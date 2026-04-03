#include "mixed_capture.h"

#include "logic_capture.h"
#include "scope_capture.h"

bool mixed_capture_start(const capture_config_t *logic_config,
                         const capture_config_t *scope_config,
                         complete_handler_t logic_handler,
                         complete_handler_t scope_handler)
{
    if (logic_config == NULL || scope_config == NULL ||
        logic_handler == NULL || scope_handler == NULL) {
        return false;
    }

    if (!logic_capture_prestart(logic_config, logic_handler)) {
        return false;
    }

    if (!scope_capture_prestart(scope_config, scope_handler)) {
        logic_capture_reset();
        return false;
    }

    logic_capture_commit_start();
    scope_capture_commit_start();

    return true;
}

void mixed_capture_reset(void)
{
    logic_capture_reset();
    scope_capture_reset();
}