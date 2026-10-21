#include "config_store.h"

#include <stdbool.h>
#include <string.h>

// In-memory, key-addressed stand-in for NVS. The long-term memory store and
// the sync record are two independent blobs, so the host stub must keep them
// apart exactly as production NVS does.
typedef struct {
    char key[CONFIG_STORE_KEY_MAX_SIZE + 1];
    unsigned char value[CONFIG_STORE_TEST_BLOB_SIZE];
    size_t value_size;
    bool is_used;
} config_store_test_slot_t;

static config_store_test_slot_t s_slots[CONFIG_STORE_TEST_SLOT_CAPACITY];
static bool s_fail_writes;

static config_store_test_slot_t *config_store_test_find(const char *key) {
    if (key == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < CONFIG_STORE_TEST_SLOT_CAPACITY; ++index) {
        if (s_slots[index].is_used &&
            strcmp(s_slots[index].key, key) == 0) {
            return &s_slots[index];
        }
    }
    return NULL;
}

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *size
) {
    if (output == NULL || size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const config_store_test_slot_t *slot = config_store_test_find(key);
    if (slot == NULL) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (*size < slot->value_size) {
        *size = slot->value_size;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, slot->value, slot->value_size);
    *size = slot->value_size;
    return ESP_OK;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t size
) {
    if (key == NULL || value == NULL || size > CONFIG_STORE_TEST_BLOB_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_fail_writes) {
        return ESP_FAIL;
    }
    config_store_test_slot_t *slot = config_store_test_find(key);
    if (slot == NULL) {
        for (size_t index = 0; index < CONFIG_STORE_TEST_SLOT_CAPACITY;
             ++index) {
            if (!s_slots[index].is_used) {
                slot = &s_slots[index];
                break;
            }
        }
    }
    if (slot == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->key, key, CONFIG_STORE_KEY_MAX_SIZE);
    memcpy(slot->value, value, size);
    slot->value_size = size;
    slot->is_used = true;
    return ESP_OK;
}

void long_term_memory_sync_test_store_reset(void) {
    memset(s_slots, 0, sizeof(s_slots));
    s_fail_writes = false;
}

void long_term_memory_sync_test_store_fail_writes(bool fails) {
    s_fail_writes = fails;
}
