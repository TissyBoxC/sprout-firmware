#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define CONFIG_STORE_VALUE_SIZE 256
#define CONFIG_STORE_SSID_SIZE 32
#define CONFIG_STORE_PASSWORD_SIZE 64

#define CONFIG_STORE_ERR_NOT_FOUND ESP_ERR_NOT_FOUND

bool config_store_is_ready(void);
esp_err_t config_store_set_string(const char *key, const char *value);
esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
);
