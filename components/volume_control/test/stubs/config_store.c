#include "config_store.h"

#include <stdio.h>
#include <string.h>

// In-memory stand-in for NVS. volume_control persists three small keys, so a
// tiny fixed table is enough to exercise first boot and restore.
#define VOLUME_TEST_MAX_ENTRIES 8
#define VOLUME_TEST_VALUE_SIZE 16

typedef struct {
    char key[CONFIG_STORE_KEY_MAX_SIZE + 1];
    char value[VOLUME_TEST_VALUE_SIZE];
    bool used;
} volume_test_entry_t;

static volume_test_entry_t s_entries[VOLUME_TEST_MAX_ENTRIES];
static bool s_ready;

void volume_test_store_reset(bool ready);
void volume_test_store_put(const char *key, const char *value);

void volume_test_store_reset(bool ready) {
    memset(s_entries, 0, sizeof(s_entries));
    s_ready = ready;
}

void volume_test_store_put(const char *key, const char *value) {
    for (size_t index = 0; index < VOLUME_TEST_MAX_ENTRIES; ++index) {
        if (s_entries[index].used &&
            strcmp(s_entries[index].key, key) == 0) {
            snprintf(s_entries[index].value, VOLUME_TEST_VALUE_SIZE, "%s", value);
            return;
        }
    }
    for (size_t index = 0; index < VOLUME_TEST_MAX_ENTRIES; ++index) {
        if (!s_entries[index].used) {
            snprintf(s_entries[index].key, sizeof(s_entries[index].key), "%s", key);
            snprintf(s_entries[index].value, VOLUME_TEST_VALUE_SIZE, "%s", value);
            s_entries[index].used = true;
            return;
        }
    }
}

bool config_store_is_ready(void) {
    return s_ready;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    if (key == NULL || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < VOLUME_TEST_MAX_ENTRIES; ++index) {
        if (!s_entries[index].used ||
            strcmp(s_entries[index].key, key) != 0) {
            continue;
        }
        if (strlen(s_entries[index].value) >= output_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        strcpy(output, s_entries[index].value);
        return ESP_OK;
    }
    return CONFIG_STORE_ERR_NOT_FOUND;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    volume_test_store_put(key, value);
    return ESP_OK;
}
