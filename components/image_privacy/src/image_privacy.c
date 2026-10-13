#include "image_privacy.h"

#include <stddef.h>

#include "config_store.h"
#include "image_privacy_sync_core.h"

// One bounded binary blob persists the guardian image-privacy policy without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define IMAGE_PRIVACY_STORE_KEY "image_privacy_policy"

static image_privacy_policy_t s_policy;
static uint32_t s_processed_count;
static uint32_t s_uploaded_count;
static uint32_t s_blocked_count;
static image_block_reason_t s_last_block_reason;
static bool s_ready;

static esp_err_t image_privacy_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        IMAGE_PRIVACY_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return IMAGE_PRIVACY_ERR_STORAGE;
    }
    return ESP_OK;
}

static void image_privacy_restore(void) {
    image_privacy_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        IMAGE_PRIVACY_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        image_privacy_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = image_privacy_default();
}

static void image_privacy_reconcile_runtime(void) {
    // A disabled or revoked policy stops processing immediately; the last block
    // reason is retained so the status report can explain the transition.
    if (!image_privacy_can_process(&s_policy)) {
        s_last_block_reason = IMAGE_BLOCK_DISABLED;
    }
}

esp_err_t image_privacy_init(void) {
    image_privacy_restore();
    s_processed_count = 0u;
    s_uploaded_count = 0u;
    s_blocked_count = 0u;
    s_last_block_reason = image_privacy_can_process(&s_policy)
                              ? IMAGE_BLOCK_NONE
                              : IMAGE_BLOCK_DISABLED;
    s_ready = true;
    return ESP_OK;
}

bool image_privacy_is_ready(void) {
    return s_ready;
}

esp_err_t image_privacy_apply(const image_privacy_policy_t *policy) {
    if (!s_ready) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    if (policy == NULL || !image_privacy_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = *policy;
    image_privacy_reconcile_runtime();
    return ESP_OK;
}

esp_err_t image_privacy_set(const image_privacy_policy_t *policy) {
    if (!s_ready) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = image_privacy_clamp(policy);
    image_privacy_reconcile_runtime();
    return image_privacy_persist();
}

esp_err_t image_privacy_apply_remote(
    const image_privacy_remote_t *remote
) {
    if (!s_ready) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    image_privacy_policy_t policy = {};
    if (!image_privacy_sync_core_build_policy(remote, &policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    return image_privacy_set(&policy);
}

image_privacy_policy_t image_privacy_get(void) {
    return s_policy;
}

bool image_privacy_processing_allowed(void) {
    return image_privacy_can_process(&s_policy);
}

esp_err_t image_privacy_begin_processing(void) {
    if (!s_ready) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    // The policy gate runs first so a disabled policy reports a permission
    // error rather than a misleading runtime-state error.
    if (!image_privacy_can_process(&s_policy)) {
        s_last_block_reason = IMAGE_BLOCK_DISABLED;
        if (s_blocked_count < IMAGE_PRIVACY_MAX_IMAGE_COUNT) {
            s_blocked_count += 1u;
        }
        return IMAGE_PRIVACY_ERR_PERMISSION_DENIED;
    }
    if (s_processed_count < IMAGE_PRIVACY_MAX_IMAGE_COUNT) {
        s_processed_count += 1u;
    }
    s_last_block_reason = IMAGE_BLOCK_NONE;
    return ESP_OK;
}

esp_err_t image_privacy_authorize_upload(image_upload_scope_t scope) {
    if (!s_ready) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    if (!image_privacy_is_valid_scope(scope)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!image_privacy_can_upload(&s_policy) ||
        (uint8_t)scope != s_policy.upload_scope) {
        s_last_block_reason = image_privacy_can_upload(&s_policy)
                                  ? IMAGE_BLOCK_SCOPE
                                  : IMAGE_BLOCK_DISABLED;
        if (s_blocked_count < IMAGE_PRIVACY_MAX_IMAGE_COUNT) {
            s_blocked_count += 1u;
        }
        return IMAGE_PRIVACY_ERR_PERMISSION_DENIED;
    }
    s_last_block_reason = IMAGE_BLOCK_NONE;
    return ESP_OK;
}

esp_err_t image_privacy_record_upload(void) {
    if (!s_ready) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    if (!image_privacy_can_upload(&s_policy)) {
        s_last_block_reason = IMAGE_BLOCK_DISABLED;
        if (s_blocked_count < IMAGE_PRIVACY_MAX_IMAGE_COUNT) {
            s_blocked_count += 1u;
        }
        return IMAGE_PRIVACY_ERR_PERMISSION_DENIED;
    }
    // Uploaded frames are a subset of processed frames, so the counter can
    // never exceed the processed ceiling.
    if (s_uploaded_count >= s_processed_count) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    s_uploaded_count += 1u;
    s_last_block_reason = IMAGE_BLOCK_NONE;
    return ESP_OK;
}

esp_err_t image_privacy_block(image_block_reason_t reason) {
    if (!s_ready) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    if (!image_privacy_is_valid_block_reason(reason) ||
        reason == IMAGE_BLOCK_NONE) {
        return ESP_ERR_INVALID_ARG;
    }
    s_last_block_reason = reason;
    if (s_blocked_count < IMAGE_PRIVACY_MAX_IMAGE_COUNT) {
        s_blocked_count += 1u;
    }
    return ESP_OK;
}

esp_err_t image_privacy_reset_counters(void) {
    if (!s_ready) {
        return IMAGE_PRIVACY_ERR_INVALID_STATE;
    }
    s_processed_count = 0u;
    s_uploaded_count = 0u;
    s_blocked_count = 0u;
    s_last_block_reason = IMAGE_BLOCK_NONE;
    return ESP_OK;
}

uint32_t image_privacy_processed_count(void) {
    return s_processed_count;
}

uint32_t image_privacy_uploaded_count(void) {
    return s_uploaded_count;
}

uint32_t image_privacy_blocked_count(void) {
    return s_blocked_count;
}

image_block_reason_t image_privacy_last_block_reason(void) {
    return s_last_block_reason;
}

static esp_err_t image_privacy_initialize(void) {
    return image_privacy_init();
}

const module_descriptor_t *image_privacy_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "image_privacy",
        .version = "1.0.0",
        .initialize = image_privacy_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
