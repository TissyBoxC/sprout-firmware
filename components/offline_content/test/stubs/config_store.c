#include "config_store.h"

#include <stdbool.h>
#include <string.h>

// In-memory stand-in for NVS so the runtime persistence path can be tested on
// the host. One blob is enough: the module persists a single envelope, and the
// realistic production NVS value is larger still.
#define OFFLINE_CONTENT_TEST_BLOB_SIZE 16384

static unsigned char s_blob[OFFLINE_CONTENT_TEST_BLOB_SIZE];
static size_t s_blob_size;
static bool s_has_blob;

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *size
) {
    (void)key;
    if (output == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_has_blob) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (*size < s_blob_size) {
        *size = s_blob_size;
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(output, s_blob, s_blob_size);
    *size = s_blob_size;
    return ESP_OK;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t size
) {
    (void)key;
    if (value == NULL || size > sizeof(s_blob)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(s_blob, value, size);
    s_blob_size = size;
    s_has_blob = true;
    return ESP_OK;
}
