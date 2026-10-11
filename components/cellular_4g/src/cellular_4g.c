#include "cellular_4g.h"

#include <stddef.h>

#include "cellular_core.h"
#include "config_store.h"

// One bounded binary blob persists the guardian cellular policy without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define CELLULAR_STORE_KEY "cellular_policy"

static cellular_policy_t s_policy;
static cellular_usage_t s_usage;
static bool s_ready;

static esp_err_t cellular_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        CELLULAR_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void cellular_restore(void) {
    cellular_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        CELLULAR_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        cellular_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = cellular_default();
}

esp_err_t cellular_init(void) {
    cellular_restore();
    cellular_usage_init(&s_usage);
    s_ready = true;
    return ESP_OK;
}

bool cellular_is_ready(void) {
    return s_ready;
}

esp_err_t cellular_apply(const cellular_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL || !cellular_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    // The bounded policy is the execution contract; the modem driver only
    // consumes validated fields from here.
    s_policy = *policy;
    return ESP_OK;
}

esp_err_t cellular_set(const cellular_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = cellular_clamp(policy);
    return cellular_persist();
}

cellular_policy_t cellular_get(void) {
    return s_policy;
}

const char *cellular_error_name(cellular_error_t error) {
    switch (error) {
        case CELLULAR_OK:
            return "ok";
        case CELLULAR_ERR_NULL_ARGUMENT:
            return "not_initialized";
        case CELLULAR_ERR_INVALID_POLICY:
            return "invalid_policy";
        case CELLULAR_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t cellular_initialize(void) {
    return cellular_init();
}

const module_descriptor_t *cellular_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "cellular_4g",
        .version = "1.0.0",
        .initialize = cellular_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
