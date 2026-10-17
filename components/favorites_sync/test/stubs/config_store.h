#pragma once

#include <stddef.h>

#include "esp_err.h"

#define CONFIG_STORE_ERR_NOT_FOUND ESP_ERR_NOT_FOUND
#define CONFIG_STORE_KEY_MAX_SIZE 15

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *value_size_in_out
);
esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t value_size
);
