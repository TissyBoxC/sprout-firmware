#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define CONFIG_STORE_KEY_MAX_SIZE 15

#ifdef __cplusplus
#define CONFIG_STORE_KEY_ASSERT(key)                                       \
    static_assert(                                                         \
        sizeof(key) <= CONFIG_STORE_KEY_MAX_SIZE + 1,                      \
        "config_store key must fit NVS_KEY_NAME_MAX_SIZE"                  \
    )
#else
#define CONFIG_STORE_KEY_ASSERT(key)                                       \
    _Static_assert(                                                        \
        sizeof(key) <= CONFIG_STORE_KEY_MAX_SIZE + 1,                      \
        "config_store key must fit NVS_KEY_NAME_MAX_SIZE"                  \
    )
#endif

bool config_store_is_ready(void);
esp_err_t config_store_erase_key(const char *key);
