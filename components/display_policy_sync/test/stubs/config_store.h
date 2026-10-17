#pragma once

#include <stddef.h>

#include "esp_err.h"

#define CONFIG_STORE_ERR_NOT_FOUND ESP_ERR_NOT_FOUND

#ifdef __cplusplus
extern "C" {
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

#ifdef __cplusplus
}
#endif
