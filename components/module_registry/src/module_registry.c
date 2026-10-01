#include "module_registry.h"

#include <string.h>

#include "esp_log.h"

#define MODULE_REGISTRY_CAPACITY CONFIG_MODULE_REGISTRY_CAPACITY

static const char *const TAG = "module_registry";

typedef struct {
    const module_descriptor_t *descriptor;
    bool is_initialized;
} module_entry_t;

static module_entry_t module_entries[MODULE_REGISTRY_CAPACITY];
static size_t module_entry_count;
static bool has_initialization_started;

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
    ++module_entry_count;
    return ESP_OK;
}

esp_err_t module_registry_initialize_all(void) {
    has_initialization_started = true;
    esp_err_t first_error = ESP_OK;

    for (size_t index = 0; index < module_entry_count; ++index) {
        module_entry_t *entry = &module_entries[index];
        const esp_err_t result = entry->descriptor->initialize();
        if (result == ESP_OK) {
            entry->is_initialized = true;
            ESP_LOGI(TAG, "initialized %s %s",
                     entry->descriptor->module_name,
                     entry->descriptor->version);
            continue;
        }

        ESP_LOGE(TAG, "%s initialization failed: %s",
                 entry->descriptor->module_name,
                 esp_err_to_name(result));
        if (first_error == ESP_OK) {
            first_error = result;
        }
    }

    return first_error;
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
    if (module_name == NULL) {
        return false;
    }
    for (size_t index = 0; index < module_entry_count; ++index) {
        if (strcmp(module_entries[index].descriptor->module_name, module_name) == 0) {
            return module_entries[index].is_initialized;
        }
    }
    return false;
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
