#pragma once

#include <stdbool.h>
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

#define CONFIG_STORE_TEST_SLOT_CAPACITY 8
#define CONFIG_STORE_TEST_BLOB_SIZE 8192

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

/** @brief Clear every stored blob between host test cases. */
void long_term_memory_sync_test_store_reset(void);

/** @brief Make the next write fail so persistence failures stay testable. */
void long_term_memory_sync_test_store_fail_writes(bool fails);

#ifdef __cplusplus
}
#endif
