#include "touch_input.h"

#include <stddef.h>

#include "config_store.h"
#include "touch_input_core.h"

// One bounded binary blob persists the guardian touch configuration without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define TOUCH_INPUT_STORE_KEY "touch_input_config"

static touch_input_config_t s_config;
static bool s_ready;

static esp_err_t touch_input_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        TOUCH_INPUT_STORE_KEY,
        &s_config,
        sizeof(s_config)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void touch_input_restore(void) {
    touch_input_config_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        TOUCH_INPUT_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        touch_input_is_valid(&restored)) {
        s_config = restored;
        return;
    }
    s_config = touch_input_default();
}

esp_err_t touch_input_init(void) {
    touch_input_restore();
    s_ready = true;
    return ESP_OK;
}

bool touch_input_is_ready(void) {
    return s_ready;
}

esp_err_t touch_input_apply(const touch_input_config_t *config) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL || !touch_input_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    // The bounded config is the execution contract; the controller only
    // consumes validated fields from here.
    s_config = *config;
    return ESP_OK;
}

esp_err_t touch_input_set(const touch_input_config_t *config) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = touch_input_clamp(config);
    return touch_input_persist();
}

touch_input_config_t touch_input_get(void) {
    return s_config;
}

const char *touch_input_error_name(touch_input_error_t error) {
    switch (error) {
        case TOUCH_INPUT_OK:
            return "ok";
        case TOUCH_INPUT_ERR_NULL_ARGUMENT:
            return "not_initialized";
        case TOUCH_INPUT_ERR_INVALID_CONFIG:
            return "invalid_config";
        case TOUCH_INPUT_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t touch_input_initialize(void) {
    return touch_input_init();
}

const module_descriptor_t *touch_input_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "touch_input",
        .version = "1.0.0",
        .initialize = touch_input_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
