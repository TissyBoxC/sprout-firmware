#pragma once

#include <stddef.h>

#include "esp_err.h"

#define CONFIG_STORE_ERR_NOT_FOUND ESP_ERR_NOT_FOUND

#define CONFIG_STORE_KEY_MAX_SIZE 15
#ifdef __cplusplus
#define CONFIG_STORE_KEY_ASSERT(key)                                       \
    static_assert(                                                        \
        sizeof(key) <= CONFIG_STORE_KEY_MAX_SIZE + 1,                     \
        "config_store key too long"                                       \
    )
#else
#define CONFIG_STORE_KEY_ASSERT(key)                                       \
    _Static_assert(                                                       \
        sizeof(key) <= CONFIG_STORE_KEY_MAX_SIZE + 1,                     \
        "config_store key too long"                                       \
    )
#endif

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *size
);
esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t size
);
