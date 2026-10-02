#include "config_store.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#define CONFIG_STORE_NVS_NAMESPACE "sprout_config"

static bool config_store_ready;

static esp_err_t config_store_open(nvs_open_mode_t mode, nvs_handle_t *handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config_store_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return nvs_open(CONFIG_STORE_NVS_NAMESPACE, mode, handle);
}

static esp_err_t config_store_validate_key(const char *key) {
    if (key == NULL || key[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(key) > NVS_KEY_NAME_MAX_SIZE - 1) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t config_store_init(void) {
    if (config_store_ready) {
        return ESP_OK;
    }

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // A full or upgraded NVS partition must be recreated here, otherwise
        // every later credential write fails with an unrelated error.
        result = nvs_flash_erase();
        if (result != ESP_OK) {
            return result;
        }
        result = nvs_flash_init();
    }
    if (result != ESP_OK) {
        return result;
    }

    config_store_ready = true;
    return ESP_OK;
}

bool config_store_is_ready(void) {
    return config_store_ready;
}

esp_err_t config_store_set_string(const char *key, const char *value) {
    const esp_err_t key_result = config_store_validate_key(key);
    if (key_result != ESP_OK) {
        return key_result;
    }
    if (value == NULL || strlen(value) > CONFIG_STORE_VALUE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result = config_store_open(NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_str(handle, key, value);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

esp_err_t config_store_get_string(
    const char *key,
    char *output,
    size_t output_size
) {
    const esp_err_t key_result = config_store_validate_key(key);
    if (key_result != ESP_OK) {
        return key_result;
    }
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result = config_store_open(NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }
    size_t required_size = output_size;
    result = nvs_get_str(handle, key, output, &required_size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    return result;
}

esp_err_t config_store_has_key(const char *key, bool *has_key_out) {
    const esp_err_t key_result = config_store_validate_key(key);
    if (key_result != ESP_OK) {
        return key_result;
    }
    if (has_key_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result = config_store_open(NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }
    size_t value_size = 0;
    result = nvs_get_str(handle, key, NULL, &value_size);
    nvs_close(handle);

    if (result == ESP_OK) {
        *has_key_out = true;
        return ESP_OK;
    }
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        *has_key_out = false;
        return ESP_OK;
    }
    return result;
}

esp_err_t config_store_erase_key(const char *key) {
    const esp_err_t key_result = config_store_validate_key(key);
    if (key_result != ESP_OK) {
        return key_result;
    }

    nvs_handle_t handle = 0;
    esp_err_t result = config_store_open(NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_erase_key(handle, key);
    // Erasing an absent key is a successful no-op for callers that reset state.
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

const module_descriptor_t *config_store_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "config_store",
        .version = "1.0.0",
        .initialize = config_store_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
