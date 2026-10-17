#include "config_store.h"

#include <stdbool.h>
#include <string.h>

#define HOST_STORE_CAPACITY 40
#define HOST_STORE_KEY_SIZE 16
#define HOST_STORE_VALUE_CAPACITY 1024

typedef struct {
    char key[HOST_STORE_KEY_SIZE];
    char value[HOST_STORE_VALUE_CAPACITY];
    bool used;
} host_store_entry_t;

static host_store_entry_t host_store[HOST_STORE_CAPACITY];
static bool host_store_ready = true;
static char host_store_fail_key[HOST_STORE_KEY_SIZE];
static char host_store_fail_erase[HOST_STORE_KEY_SIZE];

static host_store_entry_t *find_entry(const char *key) {
    if (key == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < HOST_STORE_CAPACITY; ++index) {
        if (host_store[index].used &&
            strcmp(host_store[index].key, key) == 0) {
            return &host_store[index];
        }
    }
    return NULL;
}

static host_store_entry_t *find_free_entry(void) {
    for (size_t index = 0; index < HOST_STORE_CAPACITY; ++index) {
        if (!host_store[index].used) {
            return &host_store[index];
        }
    }
    return NULL;
}

#ifdef __cplusplus
extern "C" {
#endif

void parent_policy_test_store_reset(void) {
    memset(host_store, 0, sizeof(host_store));
    host_store_ready = true;
}

void parent_policy_test_store_set_ready(bool ready) {
    host_store_ready = ready;
}

void parent_policy_test_store_set_fail_key(const char *key, bool erase) {
    char *const destination = erase ? host_store_fail_erase
                                    : host_store_fail_key;
    const char *const source = key != nullptr ? key : "";
    strncpy(destination, source, HOST_STORE_KEY_SIZE - 1);
    destination[HOST_STORE_KEY_SIZE - 1] = '\0';
}

bool parent_policy_test_store_has_key(const char *key) {
    return find_entry(key) != NULL;
}

const char *parent_policy_test_store_get(const char *key) {
    const host_store_entry_t *entry = find_entry(key);
    return entry != NULL ? entry->value : NULL;
}

bool parent_policy_test_store_set(const char *key, const char *value) {
    if (key == NULL || value == NULL ||
        strlen(key) >= HOST_STORE_KEY_SIZE ||
        strlen(value) >= HOST_STORE_VALUE_CAPACITY) {
        return false;
    }
    host_store_entry_t *entry = find_entry(key);
    if (entry == NULL) {
        entry = find_free_entry();
    }
    if (entry == NULL) {
        return false;
    }
    memset(entry, 0, sizeof(*entry));
    strcpy(entry->key, key);
    strcpy(entry->value, value);
    entry->used = true;
    return true;
}

bool parent_policy_test_store_erase(const char *key) {
    host_store_entry_t *entry = find_entry(key);
    if (entry == NULL) {
        return true;
    }
    memset(entry, 0, sizeof(*entry));
    return true;
}

#ifdef __cplusplus
}
#endif

bool config_store_is_ready(void) {
    return host_store_ready;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    if (!host_store_ready || key == NULL || output == NULL ||
        output_size == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    const host_store_entry_t *entry = find_entry(key);
    if (entry == NULL) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    if (strlen(entry->value) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(output, entry->value);
    return ESP_OK;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    if (!host_store_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_store_fail_key[0] != '\0' &&
        strcmp(host_store_fail_key, key) == 0) {
        return ESP_FAIL;
    }
    return parent_policy_test_store_set(key, value) ? ESP_OK
                                                    : ESP_ERR_NO_MEM;
}

esp_err_t config_store_erase_key(const char *key) {
    if (!host_store_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (host_store_fail_erase[0] != '\0' &&
        strcmp(host_store_fail_erase, key) == 0) {
        return ESP_FAIL;
    }
    return parent_policy_test_store_erase(key) ? ESP_OK
                                               : ESP_ERR_INVALID_STATE;
}
