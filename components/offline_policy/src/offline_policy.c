#include "offline_policy.h"

#include <stddef.h>
#include <string.h>

#include "config_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "offline_policy_core.h"

// One bounded binary blob keeps the fleet policy across restarts without a
// dedicated partition; the core store is versioned so a schema change is
// detected and replaced with the conservative default instead of applied.
#define OFFLINE_POLICY_STORE_KEY "offline_policy"

static offline_policy_t s_policy;
static SemaphoreHandle_t s_lock;
static bool s_ready;

static esp_err_t offline_policy_persist(const offline_policy_t *policy) {
    const esp_err_t stored = config_store_set_blob(
        OFFLINE_POLICY_STORE_KEY,
        policy,
        sizeof(*policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void offline_policy_restore(void) {
    offline_policy_t restored;
    memset(&restored, 0, sizeof(restored));
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        OFFLINE_POLICY_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        offline_policy_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    // A missing or malformed blob starts from the conservative default so a
    // corrupt store never blocks startup.
    s_policy = offline_policy_default();
}

esp_err_t offline_policy_init(void) {
    if (s_ready) {
        return ESP_OK;
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    offline_policy_restore();
    s_ready = true;
    return ESP_OK;
}

bool offline_policy_is_ready(void) {
    return s_ready;
}

esp_err_t offline_policy_set(const offline_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const offline_policy_t clamped = offline_policy_clamp(policy);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // An out of order delivery must never roll the cache budget back, so a
    // stale revision is acknowledged as success without changing state.
    if (!offline_policy_core_accepts_revision(
            s_policy.revision,
            clamped.revision
        )) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    const esp_err_t persisted = offline_policy_persist(&clamped);
    if (persisted == ESP_OK) {
        s_policy = clamped;
    }
    xSemaphoreGive(s_lock);
    return persisted;
}

offline_policy_t offline_policy_get(void) {
    if (!s_ready) {
        return offline_policy_default();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const offline_policy_t policy = s_policy;
    xSemaphoreGive(s_lock);
    return policy;
}

const char *offline_policy_error_name(offline_policy_error_t error) {
    switch (error) {
        case OFFLINE_POLICY_OK:
            return "ok";
        case OFFLINE_POLICY_ERR_NULL_ARGUMENT:
            return "not_initialized";
        case OFFLINE_POLICY_ERR_INVALID_POLICY:
            return "invalid_policy";
        case OFFLINE_POLICY_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t offline_policy_initialize(void) {
    return offline_policy_init();
}

const module_descriptor_t *offline_policy_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "offline_policy",
        .version = "1.0.0",
        .initialize = offline_policy_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
