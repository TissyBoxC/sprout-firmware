#include "wearable_location.h"

#include <stddef.h>

#include "config_store.h"

// One bounded binary blob persists the guardian wearable policy without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define WEARABLE_STORE_KEY "wearable_policy"

static location_policy_t s_policy;
static sos_state_t s_sos_state;
static bool s_ready;

static esp_err_t wearable_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        WEARABLE_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void wearable_restore(void) {
    location_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        WEARABLE_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        location_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = location_default();
}

esp_err_t wearable_init(void) {
    wearable_restore();
    s_sos_state = SOS_STATE_IDLE;
    s_ready = true;
    return ESP_OK;
}

bool wearable_is_ready(void) {
    return s_ready;
}

esp_err_t wearable_apply(const location_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL || !location_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = *policy;
    return ESP_OK;
}

esp_err_t wearable_set(const location_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = location_clamp(policy);
    return wearable_persist();
}

location_policy_t wearable_get(void) {
    return s_policy;
}

esp_err_t wearable_revoke_permission(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_policy.permission_granted = 0u;
    // A revoked permission must never keep an emergency latched; clear the SOS
    // so a stale trigger cannot keep reporting a position.
    s_sos_state = SOS_STATE_IDLE;
    return wearable_persist();
}

esp_err_t wearable_sos_arm(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    // Arming an emergency requires a usable position so a raised SOS can always
    // carry a location.
    if (!location_can_sample(&s_policy)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sos_state == SOS_STATE_TRIGGERED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_sos_state = SOS_STATE_ARMED;
    return ESP_OK;
}

esp_err_t wearable_sos_trigger(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!location_can_sample(&s_policy)) {
        return ESP_ERR_INVALID_STATE;
    }
    // A triggered SOS is latched; a second trigger stays triggered so a
    // momentary bounce never clears an emergency.
    s_sos_state = SOS_STATE_TRIGGERED;
    return ESP_OK;
}

esp_err_t wearable_sos_cancel(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_sos_state != SOS_STATE_TRIGGERED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_sos_state = SOS_STATE_CANCELLED;
    return ESP_OK;
}

sos_state_t wearable_sos_state(void) {
    return s_sos_state;
}

bool wearable_sos_active(void) {
    return s_sos_state == SOS_STATE_TRIGGERED;
}

static esp_err_t wearable_initialize(void) {
    return wearable_init();
}

const module_descriptor_t *wearable_location_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "wearable_location",
        .version = "1.0.0",
        .initialize = wearable_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
