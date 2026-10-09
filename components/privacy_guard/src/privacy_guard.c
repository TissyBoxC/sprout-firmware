#include "privacy_guard.h"

#include <string.h>

#include "config_store.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "privacy_guard_core.h"

static const char *const TAG = "privacy_guard";

#define PRIVACY_GUARD_CONFIG_KEY "privacy_state"

static bool privacy_guard_ready;
static privacy_guard_storage_t privacy_guard_storage;
static privacy_guard_core_state_t privacy_guard_state;
static uint32_t privacy_guard_local_deletion_total;
static privacy_guard_error_t privacy_guard_last_error = PRIVACY_GUARD_OK;

static bool privacy_guard_class_is_valid(
    privacy_guard_data_class_t data_class
) {
    return data_class >= PRIVACY_GUARD_DATA_AUDIO_UPLOAD &&
        data_class < PRIVACY_GUARD_DATA_COUNT;
}

static esp_err_t privacy_guard_nvs_load(
    bool *audio_upload_consent,
    bool *image_upload_consent,
    bool *conversation_history_consent,
    bool *usage_analytics_consent,
    uint32_t *consent_revision
) {
    if (audio_upload_consent == NULL || image_upload_consent == NULL ||
        conversation_history_consent == NULL ||
        usage_analytics_consent == NULL || consent_revision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    privacy_guard_core_state_t state;
    size_t state_size = sizeof(state);
    const esp_err_t result = config_store_get_blob(
        PRIVACY_GUARD_CONFIG_KEY,
        &state,
        &state_size
    );
    if (result == CONFIG_STORE_ERR_NOT_FOUND) {
        // A device that has never stored consent must start from deny-all.
        *audio_upload_consent = false;
        *image_upload_consent = false;
        *conversation_history_consent = false;
        *usage_analytics_consent = false;
        *consent_revision = 0;
        return ESP_OK;
    }
    if (result != ESP_OK || state_size != sizeof(state) ||
        !privacy_guard_core_state_is_compatible(&state)) {
        *audio_upload_consent = false;
        *image_upload_consent = false;
        *conversation_history_consent = false;
        *usage_analytics_consent = false;
        *consent_revision = 0;
        return ESP_OK;
    }
    *audio_upload_consent =
        state.consent[PRIVACY_GUARD_DATA_AUDIO_UPLOAD];
    *image_upload_consent =
        state.consent[PRIVACY_GUARD_DATA_IMAGE_UPLOAD];
    *conversation_history_consent =
        state.consent[PRIVACY_GUARD_DATA_CONVERSATION_HISTORY];
    *usage_analytics_consent =
        state.consent[PRIVACY_GUARD_DATA_USAGE_ANALYTICS];
    *consent_revision = state.consent_revision;
    return ESP_OK;
}

static esp_err_t privacy_guard_nvs_save(
    bool audio_upload_consent,
    bool image_upload_consent,
    bool conversation_history_consent,
    bool usage_analytics_consent,
    uint32_t consent_revision
) {
    privacy_guard_core_state_t state;
    privacy_guard_core_init(&state);
    state.consent[PRIVACY_GUARD_DATA_AUDIO_UPLOAD] =
        audio_upload_consent;
    state.consent[PRIVACY_GUARD_DATA_IMAGE_UPLOAD] =
        image_upload_consent;
    state.consent[PRIVACY_GUARD_DATA_CONVERSATION_HISTORY] =
        conversation_history_consent;
    state.consent[PRIVACY_GUARD_DATA_USAGE_ANALYTICS] =
        usage_analytics_consent;
    state.consent_revision = consent_revision;
    return config_store_set_blob(
        PRIVACY_GUARD_CONFIG_KEY,
        &state,
        sizeof(state)
    );
}

static esp_err_t privacy_guard_default_clear_cache(
    privacy_guard_action_t action
) {
    (void)action;
    return ESP_OK;
}

static privacy_guard_error_t privacy_guard_persist(void) {
    if (privacy_guard_storage.save == NULL) {
        return PRIVACY_GUARD_OK;
    }
    ++privacy_guard_state.consent_revision;
    const esp_err_t result = privacy_guard_storage.save(
        privacy_guard_state.consent[PRIVACY_GUARD_DATA_AUDIO_UPLOAD],
        privacy_guard_state.consent[PRIVACY_GUARD_DATA_IMAGE_UPLOAD],
        privacy_guard_state
            .consent[PRIVACY_GUARD_DATA_CONVERSATION_HISTORY],
        privacy_guard_state.consent[PRIVACY_GUARD_DATA_USAGE_ANALYTICS],
        privacy_guard_state.consent_revision
    );
    if (result != ESP_OK) {
        privacy_guard_last_error = PRIVACY_GUARD_ERR_STORAGE;
        return PRIVACY_GUARD_ERR_STORAGE;
    }
    return PRIVACY_GUARD_OK;
}

esp_err_t privacy_guard_init(void) {
    const privacy_guard_storage_t storage = {
        .load = privacy_guard_nvs_load,
        .save = privacy_guard_nvs_save,
        .clear_cache = privacy_guard_default_clear_cache,
    };
    return privacy_guard_init_with_storage(&storage);
}

esp_err_t privacy_guard_init_with_storage(
    const privacy_guard_storage_t *storage
) {
    if (storage == NULL || storage->load == NULL || storage->save == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (privacy_guard_ready) {
        return ESP_OK;
    }

    privacy_guard_storage = *storage;
    privacy_guard_core_init(&privacy_guard_state);
    const esp_err_t load_result = privacy_guard_storage.load(
        &privacy_guard_state.consent[PRIVACY_GUARD_DATA_AUDIO_UPLOAD],
        &privacy_guard_state.consent[PRIVACY_GUARD_DATA_IMAGE_UPLOAD],
        &privacy_guard_state
            .consent[PRIVACY_GUARD_DATA_CONVERSATION_HISTORY],
        &privacy_guard_state.consent[PRIVACY_GUARD_DATA_USAGE_ANALYTICS],
        &privacy_guard_state.consent_revision
    );
    if (load_result != ESP_OK) {
        privacy_guard_core_init(&privacy_guard_state);
        privacy_guard_last_error = PRIVACY_GUARD_ERR_STORAGE;
        return load_result;
    }
    privacy_guard_ready = true;
    privacy_guard_last_error = PRIVACY_GUARD_OK;
    ESP_LOGI(TAG, "privacy guard initialized with deny-by-default consent");
    return ESP_OK;
}

bool privacy_guard_is_ready(void) {
    return privacy_guard_ready;
}

privacy_guard_snapshot_t privacy_guard_get_snapshot(void) {
    privacy_guard_snapshot_t snapshot = {
        .is_initialized = privacy_guard_ready,
        .audio_upload_consent =
            privacy_guard_state.consent[PRIVACY_GUARD_DATA_AUDIO_UPLOAD],
        .image_upload_consent =
            privacy_guard_state.consent[PRIVACY_GUARD_DATA_IMAGE_UPLOAD],
        .conversation_history_consent = privacy_guard_state
            .consent[PRIVACY_GUARD_DATA_CONVERSATION_HISTORY],
        .usage_analytics_consent =
            privacy_guard_state.consent[PRIVACY_GUARD_DATA_USAGE_ANALYTICS],
        .local_deletion_count = privacy_guard_local_deletion_total,
        .consent_revision = privacy_guard_state.consent_revision,
        .last_error = privacy_guard_last_error,
    };
    return snapshot;
}

bool privacy_guard_has_consent(privacy_guard_data_class_t data_class) {
    if (!privacy_guard_ready || !privacy_guard_class_is_valid(data_class)) {
        return false;
    }
    return privacy_guard_core_is_allowed(&privacy_guard_state, data_class);
}

privacy_guard_error_t privacy_guard_evaluate(
    privacy_guard_data_class_t data_class,
    privacy_guard_decision_t *decision_out
) {
    if (decision_out == NULL) {
        return PRIVACY_GUARD_ERR_NULL_ARGUMENT;
    }
    if (!privacy_guard_class_is_valid(data_class)) {
        decision_out->data_class = data_class;
        decision_out->allowed = false;
        decision_out->guardian_consent = false;
        decision_out->policy_requires_consent = true;
        decision_out->reason = PRIVACY_GUARD_ERR_INVALID_CLASS;
        return PRIVACY_GUARD_ERR_INVALID_CLASS;
    }
    const bool consent = privacy_guard_ready &&
        privacy_guard_core_is_allowed(&privacy_guard_state, data_class);
    decision_out->data_class = data_class;
    decision_out->allowed = consent;
    decision_out->guardian_consent = consent;
    decision_out->policy_requires_consent = true;
    decision_out->reason = consent ? PRIVACY_GUARD_OK
                                   : PRIVACY_GUARD_ERR_CONSENT_REQUIRED;
    return decision_out->reason;
}

privacy_guard_error_t privacy_guard_grant_consent(
    privacy_guard_data_class_t data_class
) {
    if (!privacy_guard_ready) {
        return PRIVACY_GUARD_ERR_NOT_INITIALIZED;
    }
    if (!privacy_guard_class_is_valid(data_class)) {
        return PRIVACY_GUARD_ERR_INVALID_CLASS;
    }
    privacy_guard_state.consent[(size_t)data_class] = true;
    return privacy_guard_persist();
}

privacy_guard_error_t privacy_guard_revoke_consent(
    privacy_guard_data_class_t data_class
) {
    if (!privacy_guard_ready) {
        return PRIVACY_GUARD_ERR_NOT_INITIALIZED;
    }
    if (!privacy_guard_class_is_valid(data_class)) {
        return PRIVACY_GUARD_ERR_INVALID_CLASS;
    }
    privacy_guard_state.consent[(size_t)data_class] = false;
    return privacy_guard_persist();
}

privacy_guard_error_t privacy_guard_revoke_all(void) {
    if (!privacy_guard_ready) {
        return PRIVACY_GUARD_ERR_NOT_INITIALIZED;
    }
    for (size_t index = 0; index < PRIVACY_GUARD_CORE_CLASS_COUNT; ++index) {
        privacy_guard_state.consent[index] = false;
    }
    return privacy_guard_persist();
}

privacy_guard_error_t privacy_guard_clear_local_data(
    privacy_guard_action_t action
) {
    if (!privacy_guard_ready) {
        return PRIVACY_GUARD_ERR_NOT_INITIALIZED;
    }
    if (privacy_guard_storage.clear_cache == NULL) {
        return PRIVACY_GUARD_ERR_STORAGE;
    }
    const esp_err_t result = privacy_guard_storage.clear_cache(action);
    if (result != ESP_OK) {
        privacy_guard_last_error = PRIVACY_GUARD_ERR_STORAGE;
        return PRIVACY_GUARD_ERR_STORAGE;
    }
    ++privacy_guard_local_deletion_total;
    privacy_guard_last_error = PRIVACY_GUARD_OK;
    return PRIVACY_GUARD_OK;
}

uint32_t privacy_guard_local_deletion_count(void) {
    return privacy_guard_local_deletion_total;
}

const char *privacy_guard_data_class_name(
    privacy_guard_data_class_t data_class
) {
    switch (data_class) {
        case PRIVACY_GUARD_DATA_AUDIO_UPLOAD:
            return "audio_upload";
        case PRIVACY_GUARD_DATA_IMAGE_UPLOAD:
            return "image_upload";
        case PRIVACY_GUARD_DATA_CONVERSATION_HISTORY:
            return "conversation_history";
        case PRIVACY_GUARD_DATA_USAGE_ANALYTICS:
            return "usage_analytics";
        default:
            return "unknown";
    }
}

const char *privacy_guard_error_name(privacy_guard_error_t error) {
    switch (error) {
        case PRIVACY_GUARD_OK:
            return "ok";
        case PRIVACY_GUARD_ERR_NULL_ARGUMENT:
            return "null_argument";
        case PRIVACY_GUARD_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case PRIVACY_GUARD_ERR_INVALID_CLASS:
            return "invalid_data_class";
        case PRIVACY_GUARD_ERR_STORAGE:
            return "storage_error";
        case PRIVACY_GUARD_ERR_CONSENT_REQUIRED:
            return "guardian_consent_required";
        case PRIVACY_GUARD_ERR_CONSENT_REVOKED:
            return "guardian_consent_revoked";
        case PRIVACY_GUARD_ERR_CACHE_BUSY:
            return "cache_busy";
        default:
            return "unknown";
    }
}

const module_descriptor_t *privacy_guard_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "privacy_guard",
        .version = "1.0.0",
        .initialize = privacy_guard_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
