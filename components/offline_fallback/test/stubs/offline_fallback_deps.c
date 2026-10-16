#include "config_store.h"
#include "esp_timer.h"
#include "network_manager.h"
#include "time_sync.h"

#include <stdio.h>
#include <string.h>

#define HOST_STORE_MAX_ENTRIES 4
#define HOST_STORE_VALUE_SIZE 32

typedef struct {
    char key[CONFIG_STORE_KEY_MAX_SIZE + 1];
    char value[HOST_STORE_VALUE_SIZE];
    bool used;
} host_store_entry_t;

static host_store_entry_t host_store_entries[HOST_STORE_MAX_ENTRIES];
static bool host_store_ready = true;
static esp_err_t host_store_set_result = ESP_OK;

static network_manager_state_t host_network_state = NETWORK_MANAGER_STATE_CONNECTED;
static network_manager_state_callback_t host_network_callback;
static bool host_time_synchronized = true;
static int host_timer_storage;
static int host_timer_starts;
static int host_timer_stops;
static int host_timer_creates;
static esp_err_t host_timer_create_result = ESP_OK;

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
    if (host_store_set_result != ESP_OK) {
        return host_store_set_result;
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

network_manager_state_t network_manager_get_state(void) {
    return host_network_state;
}

esp_err_t network_manager_add_state_callback(
    network_manager_state_callback_t callback,
    void *context
) {
    (void)context;
    host_network_callback = callback;
    return ESP_OK;
}

bool time_sync_is_synchronized(void) {
    return host_time_synchronized;
}

esp_err_t esp_timer_create(
    const esp_timer_create_args_t *args,
    esp_timer_handle_t *handle_out
) {
    ++host_timer_creates;
    if (host_timer_create_result != ESP_OK) {
        return host_timer_create_result;
    }
    if (args == NULL || handle_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *handle_out = (esp_timer_handle_t)&host_timer_storage;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us) {
    (void)handle;
    (void)timeout_us;
    ++host_timer_starts;
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t handle) {
    (void)handle;
    ++host_timer_stops;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

// Test hooks.
void offline_host_reset_store(bool ready) {
    memset(host_store_entries, 0, sizeof(host_store_entries));
    host_store_ready = ready;
    host_store_set_result = ESP_OK;
}

void offline_host_put(const char *key, const char *value) {
    const esp_err_t saved = host_store_set_result;
    host_store_set_result = ESP_OK;
    (void)config_store_set_string(key, value);
    host_store_set_result = saved;
}

void offline_host_set_store_write_result(esp_err_t result) {
    host_store_set_result = result;
}

void offline_host_set_network_state(network_manager_state_t state) {
    host_network_state = state;
}

void offline_host_emit_network_state(network_manager_state_t state) {
    host_network_state = state;
    if (host_network_callback != NULL) {
        host_network_callback(state, NULL);
    }
}

void offline_host_set_time_synchronized(bool synchronized) {
    host_time_synchronized = synchronized;
}

int offline_host_timer_starts(void) {
    return host_timer_starts;
}

int offline_host_timer_stops(void) {
    return host_timer_stops;
}

int offline_host_timer_creates(void) {
    return host_timer_creates;
}

void offline_host_set_timer_create_result(esp_err_t result) {
    host_timer_create_result = result;
}
