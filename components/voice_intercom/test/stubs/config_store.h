#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_STORE_KEY_MAX_SIZE 15

/** @brief Read one string value; missing keys return ESP_ERR_NOT_FOUND. */
esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
);

#ifdef __cplusplus
}
#endif
