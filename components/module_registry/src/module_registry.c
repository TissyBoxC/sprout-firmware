#include "module_registry.h"

#include <string.h>

#include "esp_log.h"

#define MODULE_REGISTRY_CAPACITY CONFIG_MODULE_REGISTRY_CAPACITY

static const char *const TAG = "module_registry";

typedef struct {
    const module_descriptor_t *descriptor;
    bool is_initialized;
    bool has_failed;
    esp_err_t last_error;
} module_entry_t;

static module_entry_t module_entries[MODULE_REGISTRY_CAPACITY];
static size_t module_entry_count;
static bool has_initialization_started;
static module_failure_handler_t failure_handler;
static module_recovery_handler_t recovery_handler;

static module_entry_t *find_module_entry(const char *module_name) {
    if (module_name == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < module_entry_count; ++index) {
        if (strcmp(module_entries[index].descriptor->module_name,
                   module_name) == 0) {
            return &module_entries[index];
        }
    }
    return NULL;
}

static esp_err_t initialize_module_entry(module_entry_t *entry) {
    const esp_err_t result = entry->descriptor->initialize();
    if (result == ESP_OK) {
        const bool was_failed = entry->has_failed;
        entry->is_initialized = true;
        entry->has_failed = false;
        entry->last_error = ESP_OK;
        ESP_LOGI(TAG, "initialized %s %s",
                 entry->descriptor->module_name,
                 entry->descriptor->version);
        if (was_failed && recovery_handler != NULL) {
            recovery_handler(entry->descriptor->module_name);
        }
        return ESP_OK;
    }

    entry->is_initialized = false;
    entry->has_failed = true;
    entry->last_error = result;
    ESP_LOGE(TAG, "%s initialization failed: %s",
             entry->descriptor->module_name,
             esp_err_to_name(result));
    if (failure_handler != NULL) {
        failure_handler(entry->descriptor->module_name, result);
    }
    return result;
}

esp_err_t module_registry_add(const module_descriptor_t *descriptor) {
    if (descriptor == NULL || descriptor->module_name == NULL ||
        descriptor->version == NULL || descriptor->initialize == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (has_initialization_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (module_entry_count >= MODULE_REGISTRY_CAPACITY) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t index = 0; index < module_entry_count; ++index) {
        if (strcmp(module_entries[index].descriptor->module_name,
                   descriptor->module_name) == 0) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    module_entries[module_entry_count].descriptor = descriptor;
    module_entries[module_entry_count].is_initialized = false;
    module_entries[module_entry_count].has_failed = false;
    module_entries[module_entry_count].last_error = ESP_OK;
    ++module_entry_count;
    return ESP_OK;
}

esp_err_t module_registry_initialize_all(void) {
    has_initialization_started = true;
    esp_err_t first_error = ESP_OK;

    for (size_t index = 0; index < module_entry_count; ++index) {
        module_entry_t *entry = &module_entries[index];
        const esp_err_t result = initialize_module_entry(entry);
        if (result != ESP_OK && first_error == ESP_OK) {
            first_error = result;
        }
    }

    // Retry after every other module had a chance to initialize. This gives
    // transient dependencies a deterministic recovery path without requiring
    // the composition root to duplicate registry state.
    for (size_t index = 0; index < module_entry_count; ++index) {
        module_entry_t *entry = &module_entries[index];
        if (!entry->has_failed) {
            continue;
        }
        (void)initialize_module_entry(entry);
    }

    for (size_t index = 0; index < module_entry_count; ++index) {
        if (!module_entries[index].is_initialized) {
            return first_error;
        }
    }
    return ESP_OK;
}

esp_err_t module_registry_initialize_module(const char *module_name) {
    if (!has_initialization_started) {
        return ESP_ERR_INVALID_STATE;
    }
    module_entry_t *entry = find_module_entry(module_name);
    if (entry == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (entry->is_initialized) {
        return ESP_OK;
    }
    if (!entry->has_failed) {
        return ESP_ERR_INVALID_STATE;
    }
    return initialize_module_entry(entry);
}

void module_registry_set_failure_handler(
    module_failure_handler_t handler
) {
    failure_handler = handler;
    if (handler == NULL) {
        return;
    }

    // A failure can occur before the recovery owner is initialized. Replay
    // current failures once so diagnostics still see the original cause.
    for (size_t index = 0; index < module_entry_count; ++index) {
        const module_entry_t *entry = &module_entries[index];
        if (entry->has_failed) {
            handler(entry->descriptor->module_name, entry->last_error);
        }
    }
}

void module_registry_set_recovery_handler(
    module_recovery_handler_t handler
) {
    recovery_handler = handler;
}

bool module_registry_has_failed(const char *module_name) {
    const module_entry_t *entry = find_module_entry(module_name);
    return entry != NULL && entry->has_failed;
}

void module_registry_shutdown_all(void) {
    for (size_t index = module_entry_count; index > 0; --index) {
        module_entry_t *entry = &module_entries[index - 1];
        if (!entry->is_initialized) {
            continue;
        }
        if (entry->descriptor->shutdown != NULL) {
            entry->descriptor->shutdown();
        }
        entry->is_initialized = false;
    }
    has_initialization_started = false;
}

bool module_registry_is_initialized(const char *module_name) {
    const module_entry_t *entry = find_module_entry(module_name);
    return entry != NULL && entry->is_initialized;
}

size_t module_registry_initialized_count(void) {
    size_t count = 0;
    for (size_t index = 0; index < module_entry_count; ++index) {
        if (module_entries[index].is_initialized) {
            ++count;
        }
    }
    return count;
}
