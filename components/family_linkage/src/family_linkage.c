#include "family_linkage.h"

#include <stddef.h>

#include "config_store.h"

// One bounded binary blob persists the family policy without a dedicated
// partition; a schema change is detected and replaced with the conservative
// default instead of applied.
#define FAMILY_STORE_KEY "family_policy"

static family_policy_t s_policy;
static sync_state_t s_sync_state;
static uint8_t s_sync_attempts;
static bool s_ready;

static esp_err_t family_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        FAMILY_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void family_restore(void) {
    family_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        FAMILY_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        family_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = family_default();
}

esp_err_t family_init(void) {
    family_restore();
    s_sync_state = SYNC_STATE_IDLE;
    s_sync_attempts = 0u;
    s_ready = true;
    return ESP_OK;
}

bool family_is_ready(void) {
    return s_ready;
}

esp_err_t family_apply(const family_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL || !family_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = *policy;
    return ESP_OK;
}

esp_err_t family_set(const family_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = family_clamp(policy);
    return family_persist();
}

family_policy_t family_get(void) {
    return s_policy;
}

sync_state_t family_record_sync(sync_version_pair_t versions) {
    if (!s_ready) {
        return SYNC_STATE_FAILED;
    }
    const sync_state_t next = family_sync_state(&s_policy, versions);
    s_sync_state = next;
    if (next == SYNC_STATE_APPLIED) {
        // A successful sync resets the retry budget so a later change starts
        // fresh.
        s_sync_attempts = 0u;
    } else {
        s_sync_attempts =
            s_sync_attempts < FAMILY_MAX_SYNC_ATTEMPTS
                ? (uint8_t)(s_sync_attempts + 1u)
                : s_sync_attempts;
    }
    return next;
}

sync_state_t family_last_sync_state(void) {
    return s_sync_state;
}

bool family_sync_exhausted(void) {
    return s_sync_attempts >= s_policy.sync_max_attempts;
}

static esp_err_t family_initialize(void) {
    return family_init();
}

const module_descriptor_t *family_linkage_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "family_linkage",
        .version = "1.0.0",
        .initialize = family_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
