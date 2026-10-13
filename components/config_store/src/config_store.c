#include "config_store.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define CONFIG_STORE_NVS_NAMESPACE "sprout_config"
#ifndef CONFIG_CONFIG_STORE_LARGE_PARTITION
#define CONFIG_CONFIG_STORE_LARGE_PARTITION "nvs_data"
#endif

static const char *const TAG = "config_store";

static bool config_store_ready;
static bool config_store_large_ready;

static const char *config_store_large_partition(void) {
    return CONFIG_CONFIG_STORE_LARGE_PARTITION;
}

static bool config_store_uses_large_partition(size_t value_size) {
    return config_store_large_ready &&
           value_size > CONFIG_STORE_LARGE_BLOB_THRESHOLD;
}

static esp_err_t config_store_init_large_partition(void) {
#if CONFIG_NVS_ENCRYPTION
    nvs_sec_cfg_t cfg = {0};
    esp_err_t result = nvs_flash_read_security_cfg_v2(
        nvs_flash_get_default_security_scheme(),
        &cfg
    );
    if (result != ESP_OK) {
        result = nvs_flash_generate_keys_v2(
            nvs_flash_get_default_security_scheme(),
            &cfg
        );
        if (result != ESP_OK) {
            return result;
        }
    }
    return nvs_flash_secure_init_partition(
        config_store_large_partition(),
        &cfg
    );
#else
    return nvs_flash_init_partition(config_store_large_partition());
#endif
}

static esp_err_t config_store_open_partition(
    const char *partition,
    nvs_open_mode_t mode,
    nvs_handle_t *handle
) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config_store_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (partition == NULL) {
        return nvs_open(CONFIG_STORE_NVS_NAMESPACE, mode, handle);
    }
    return nvs_open_from_partition(
        partition,
        CONFIG_STORE_NVS_NAMESPACE,
        mode,
        handle
    );
}

static esp_err_t config_store_open(nvs_open_mode_t mode, nvs_handle_t *handle) {
    return config_store_open_partition(NULL, mode, handle);
}

static esp_err_t config_store_open_large(
    nvs_open_mode_t mode,
    nvs_handle_t *handle
) {
    if (!config_store_large_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return config_store_open_partition(
        config_store_large_partition(),
        mode,
        handle
    );
}

static esp_err_t config_store_validate_key(const char *key) {
    if (key == NULL || key[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(key) > CONFIG_STORE_KEY_MAX_SIZE) {
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

    const char *large_partition = config_store_large_partition();
    const esp_partition_t *large = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS,
        large_partition
    );
    if (large == NULL) {
        // An app-only OTA does not rewrite the partition table, so a device
        // updated in the field may still lack the large partition. Falling
        // back to the default partition keeps credentials and small records
        // working instead of failing every persistence call.
        ESP_LOGW(
            TAG,
            "large partition '%s' missing; large blobs stay in default nvs",
            large_partition
        );
    } else {
        result = config_store_init_large_partition();
        if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
            result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            result = nvs_flash_erase_partition(large_partition);
            if (result != ESP_OK) {
                return result;
            }
            result = config_store_init_large_partition();
        }
        if (result != ESP_OK) {
            return result;
        }
        config_store_large_ready = true;
    }

    config_store_ready = true;
    return ESP_OK;
}

bool config_store_is_ready(void) {
    return config_store_ready;
}

esp_err_t config_store_set_blob(
    const char *key,
    const void *value,
    size_t value_size
) {
    const esp_err_t key_result = config_store_validate_key(key);
    if (key_result != ESP_OK) {
        return key_result;
    }
    if (value == NULL || value_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result = config_store_uses_large_partition(value_size)
                           ? config_store_open_large(NVS_READWRITE, &handle)
                           : config_store_open(NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(handle, key, value, value_size);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

esp_err_t config_store_get_blob(
    const char *key,
    void *output,
    size_t *value_size_in_out
) {
    const esp_err_t key_result = config_store_validate_key(key);
    if (key_result != ESP_OK) {
        return key_result;
    }
    if (output == NULL || value_size_in_out == NULL ||
        *value_size_in_out == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t result = config_store_uses_large_partition(*value_size_in_out)
                           ? config_store_open_large(NVS_READONLY, &handle)
                           : config_store_open(NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_get_blob(
        handle,
        key,
        output,
        value_size_in_out
    );
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND &&
        config_store_uses_large_partition(*value_size_in_out)) {
        result = config_store_open(NVS_READONLY, &handle);
        if (result == ESP_OK) {
            result = nvs_get_blob(handle, key, output, value_size_in_out);
            nvs_close(handle);
        }
    }
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return CONFIG_STORE_ERR_NOT_FOUND;
    }
    return result;
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
    if (result == ESP_OK) {
        size_t value_size = 0;
        result = nvs_get_str(handle, key, NULL, &value_size);
        if (result == ESP_ERR_NVS_NOT_FOUND ||
            result == ESP_ERR_NVS_TYPE_MISMATCH) {
            result = nvs_get_blob(handle, key, NULL, &value_size);
        }
        nvs_close(handle);
    }
    if (result == ESP_OK) {
        *has_key_out = true;
        return ESP_OK;
    }
    if (result == ESP_ERR_NVS_NOT_FOUND && config_store_large_ready) {
        result = config_store_open_large(NVS_READONLY, &handle);
        if (result == ESP_OK) {
            size_t value_size = 0;
            result = nvs_get_blob(handle, key, NULL, &value_size);
            if (result == ESP_ERR_NVS_NOT_FOUND ||
                result == ESP_ERR_NVS_TYPE_MISMATCH) {
                result = nvs_get_str(handle, key, NULL, &value_size);
            }
            nvs_close(handle);
        }
    }
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
    esp_err_t erase_result = nvs_erase_key(handle, key);
    if (erase_result == ESP_OK) {
        erase_result = nvs_commit(handle);
    }
    nvs_close(handle);

    if (config_store_large_ready) {
        result = config_store_open_large(NVS_READWRITE, &handle);
        if (result != ESP_OK) {
            return result;
        }
        const esp_err_t large_erase_result = nvs_erase_key(handle, key);
        if (large_erase_result == ESP_OK) {
            result = nvs_commit(handle);
        } else if (large_erase_result != ESP_ERR_NVS_NOT_FOUND) {
            result = large_erase_result;
        } else {
            result = ESP_OK;
        }
        nvs_close(handle);
        if (result != ESP_OK) {
            return result;
        }
    }

    if (erase_result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    return erase_result;
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
