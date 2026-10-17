#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOLUME_CONTROL_OK = 0,
    VOLUME_CONTROL_ERR_NOT_INITIALIZED,
    VOLUME_CONTROL_ERR_INVALID_PERCENT,
    VOLUME_CONTROL_ERR_STORAGE,
} volume_control_error_t;

bool volume_control_is_ready(void);
volume_control_error_t volume_control_set_max_percent(
    uint8_t max_volume_percent
);

#ifdef __cplusplus
}
#endif
