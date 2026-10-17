#include "config_store.h"

#include <string.h>

#define PLAYBACK_HISTORY_TEST_BLOB_SIZE 16384

static unsigned char host_blob[PLAYBACK_HISTORY_TEST_BLOB_SIZE];
static size_t host_blob_size;
static bool host_blob_present;
static bool host_store_ready = true;

void playback_history_test_store_reset(void) {
    (void)memset(host_blob, 0, sizeof(host_blob));
    host_blob_size = 0;
    host_blob_present = false;
    host_store_ready = true;
}

bool playback_history_test_store_is_ready(void) {
    return host_store_ready;
}

void playback_history_test_store_set_ready(bool ready) {
    host_store_ready = ready;
}

bool playback_history_test_store_has_blob(void) {
    return host_blob_present;
}

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *size
) {
    (void)key;
    if (!host_store_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (output == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!host_blob_present) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (*size < host_blob_size) {
        *size = host_blob_size;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, host_blob, host_blob_size);
    *size = host_blob_size;
    return ESP_OK;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t size
) {
    (void)key;
    if (!host_store_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (value == NULL || size > sizeof(host_blob)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(host_blob, value, size);
    host_blob_size = size;
    host_blob_present = true;
    return ESP_OK;
}
