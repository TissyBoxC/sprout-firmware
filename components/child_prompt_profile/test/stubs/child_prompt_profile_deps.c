#include "config_store.h"
#include "parent_policy.h"

#include <stdio.h>
#include <string.h>

// In-memory config store keyed by a single small table; child_prompt_profile
// only persists the age tier.
#define HOST_STORE_MAX_ENTRIES 4
#define HOST_STORE_VALUE_SIZE 32

typedef struct {
    char key[CONFIG_STORE_KEY_MAX_SIZE + 1];
    char value[HOST_STORE_VALUE_SIZE];
    bool used;
} host_store_entry_t;

static host_store_entry_t host_store_entries[HOST_STORE_MAX_ENTRIES];
static bool host_store_ready = true;

static parent_policy_snapshot_t host_policy_snapshot;
static bool host_policy_present;
static parent_policy_change_callback_t host_policy_observer;

bool config_store_is_ready(void) {
    return host_store_ready;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    size_t index;
    if (key == NULL || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (index = 0; index < HOST_STORE_MAX_ENTRIES; ++index) {
        if (!host_store_entries[index].used ||
            strcmp(host_store_entries[index].key, key) != 0) {
            continue;
        }
        if (strlen(host_store_entries[index].value) >= output_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        strcpy(output, host_store_entries[index].value);
        return ESP_OK;
    }
    return CONFIG_STORE_ERR_NOT_FOUND;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    size_t index;
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (index = 0; index < HOST_STORE_MAX_ENTRIES; ++index) {
        if (host_store_entries[index].used &&
            strcmp(host_store_entries[index].key, key) == 0) {
            snprintf(host_store_entries[index].value, HOST_STORE_VALUE_SIZE,
                     "%s", value);
            return ESP_OK;
        }
    }
    for (index = 0; index < HOST_STORE_MAX_ENTRIES; ++index) {
        if (!host_store_entries[index].used) {
            snprintf(host_store_entries[index].key,
                     sizeof(host_store_entries[index].key), "%s", key);
            snprintf(host_store_entries[index].value, HOST_STORE_VALUE_SIZE,
                     "%s", value);
            host_store_entries[index].used = true;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t parent_policy_get_snapshot(parent_policy_snapshot_t *snapshot_out) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!host_policy_present) {
        return ESP_ERR_NOT_FOUND;
    }
    *snapshot_out = host_policy_snapshot;
    return ESP_OK;
}

esp_err_t parent_policy_add_change_observer(
    parent_policy_change_callback_t callback,
    void *context
) {
    (void)context;
    host_policy_observer = callback;
    return ESP_OK;
}

// Test hooks.
void child_prompt_host_reset_store(bool ready) {
    memset(host_store_entries, 0, sizeof(host_store_entries));
    host_store_ready = ready;
}

void child_prompt_host_put(const char *key, const char *value) {
    (void)config_store_set_string(key, value);
}

void child_prompt_host_set_policy_present(bool present) {
    host_policy_present = present;
}

parent_policy_snapshot_t *child_prompt_host_policy(void) {
    memset(&host_policy_snapshot, 0, sizeof(host_policy_snapshot));
    host_policy_snapshot.max_volume_percent = 100;
    return &host_policy_snapshot;
}

void child_prompt_host_set_policy_category(
    size_t index,
    const char *category
) {
    if (index >= PARENT_POLICY_MAX_ALLOWED_CATEGORIES) {
        return;
    }
    strncpy(
        host_policy_snapshot.allowed_categories[index],
        category,
        PARENT_POLICY_CATEGORY_SIZE - 1
    );
}
