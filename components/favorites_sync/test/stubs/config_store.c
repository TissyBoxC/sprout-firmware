#include "config_store.h"

#include <stdbool.h>
#include <string.h>

#define FAVORITES_TEST_BLOB_SIZE 65536

static unsigned char s_blob[FAVORITES_TEST_BLOB_SIZE];
static size_t s_blob_size;
static bool s_has_blob;

#ifdef __cplusplus
extern "C" {
#endif

void favorites_test_store_reset(void) {
    s_blob_size = 0;
    s_has_blob = false;
}

#ifdef __cplusplus
}
#endif

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *value_size_in_out
) {
    (void)key;
    if (output == NULL || value_size_in_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_has_blob) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (*value_size_in_out < s_blob_size) {
        *value_size_in_out = s_blob_size;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, s_blob, s_blob_size);
    *value_size_in_out = s_blob_size;
    return ESP_OK;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t value_size
) {
    (void)key;
    if (value == NULL || value_size > sizeof(s_blob)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_blob, value, value_size);
    s_blob_size = value_size;
    s_has_blob = true;
    return ESP_OK;
}
