#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define CONFIG_STORE_ERR_NOT_FOUND ESP_ERR_NOT_FOUND
#define CONFIG_STORE_KEY_MAX_SIZE 15
#define CONFIG_STORE_VALUE_SIZE 512

#ifdef __cplusplus
extern "C" {
#endif

bool config_store_is_ready(void);
esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
);
esp_err_t config_store_set_string(const char *key, const char *value);
esp_err_t config_store_erase_key(const char *key);
void parent_policy_test_store_set_fail_key(const char *key, bool erase);

#ifdef __cplusplus
}
#endif
