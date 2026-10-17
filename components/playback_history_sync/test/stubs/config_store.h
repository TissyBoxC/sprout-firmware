#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define CONFIG_STORE_ERR_NOT_FOUND ESP_ERR_NOT_FOUND

#ifdef __cplusplus
#define CONFIG_STORE_KEY_ASSERT(key)                                       \
    static_assert(                                                         \
        sizeof(key) <= 16,                                                 \
        "config_store key too long"                                        \
    )
#else
#define CONFIG_STORE_KEY_ASSERT(key)                                       \
    _Static_assert(sizeof(key) <= 16, "config_store key too long")
#endif

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

void playback_history_test_store_reset(void);
bool playback_history_test_store_is_ready(void);
void playback_history_test_store_set_ready(bool ready);
bool playback_history_test_store_has_blob(void);

#ifdef __cplusplus
}
#endif
