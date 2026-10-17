#include "config_store.h"

#include <string.h>

#define LOCAL_COMMAND_TEST_BLOB_SIZE 32768

static unsigned char s_blob[LOCAL_COMMAND_TEST_BLOB_SIZE];
static size_t s_blob_size;
static bool s_has_blob;

#ifdef __cplusplus
extern "C" {
#endif

void local_command_test_store_reset(void) {
    s_blob_size = 0;
    s_has_blob = false;
}

#ifdef __cplusplus
}
#endif

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
        return ESP_ERR_INVALID_SIZE;
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
