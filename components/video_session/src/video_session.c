#include "video_session.h"

#include <stddef.h>

#include "config_store.h"
#include "video_session_sync_core.h"

// One bounded binary blob persists the guardian video policy without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define VIDEO_STORE_KEY "video_policy"

static video_policy_t s_policy;
static video_state_t s_state;
static video_quality_t s_quality;
static video_end_reason_t s_end_reason;
static bool s_ready;

static esp_err_t video_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        VIDEO_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void video_restore(void) {
    video_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        VIDEO_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        video_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = video_default();
}

esp_err_t video_init(void) {
    video_restore();
    s_state = VIDEO_STATE_IDLE;
    s_quality = VIDEO_QUALITY_UNKNOWN;
    s_end_reason = VIDEO_END_NONE;
    s_ready = true;
    return ESP_OK;
}

bool video_is_ready(void) {
    return s_ready;
}

esp_err_t video_apply(const video_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL || !video_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = *policy;
    return ESP_OK;
}

esp_err_t video_set(const video_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = video_clamp(policy);
    return video_persist();
}

esp_err_t video_apply_remote(const video_remote_t *remote) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    video_policy_t policy = {};
    if (!video_session_sync_core_build_policy(remote, &policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    return video_set(&policy);
}

video_policy_t video_get(void) {
    return s_policy;
}

esp_err_t video_ring(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state == VIDEO_STATE_ACTIVE || s_state == VIDEO_STATE_RINGING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_policy.enabled == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    // A call must be able to carry at least the microphone; a camera-only body
    // is refused so the platform never advertises a call it cannot carry.
    if (s_policy.microphone_allowed == 0u &&
        s_policy.camera_allowed == 0u) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state = VIDEO_STATE_RINGING;
    s_quality = VIDEO_QUALITY_UNKNOWN;
    s_end_reason = VIDEO_END_NONE;
    return ESP_OK;
}

esp_err_t video_accept(void) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state != VIDEO_STATE_RINGING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!video_can_capture(&s_policy)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_state = VIDEO_STATE_ACTIVE;
    return ESP_OK;
}

esp_err_t video_end(video_end_reason_t reason) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    // A call can only be ended while it is idle-to-terminal; an already
    // terminal call rejects a second end so the state stays stable.
    if (s_state == VIDEO_STATE_IDLE || s_state == VIDEO_STATE_ENDED ||
        s_state == VIDEO_STATE_REJECTED || s_state == VIDEO_STATE_FAILED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_end_reason = reason;
    if (reason == VIDEO_END_REJECTED) {
        s_state = VIDEO_STATE_REJECTED;
    } else if (reason == VIDEO_END_NETWORK ||
               reason == VIDEO_END_PERMISSION_REVOKED) {
        s_state = VIDEO_STATE_FAILED;
    } else {
        s_state = VIDEO_STATE_ENDED;
    }
    return ESP_OK;
}

esp_err_t video_report_quality(video_quality_t quality) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (quality > VIDEO_QUALITY_POOR) {
        return ESP_ERR_INVALID_ARG;
    }
    s_quality = quality;
    return ESP_OK;
}

esp_err_t video_revoke_permission(bool camera, bool microphone) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool was_capturing = s_state == VIDEO_STATE_ACTIVE;
    if (camera) {
        s_policy.camera_allowed = 0u;
    }
    if (microphone) {
        s_policy.microphone_allowed = 0u;
    }
    // A revoked permission must never leave an active capture running; end the
    // call immediately with a first-class reason.
    if (was_capturing) {
        s_end_reason = VIDEO_END_PERMISSION_REVOKED;
        s_state = VIDEO_STATE_FAILED;
    }
    return ESP_OK;
}

video_state_t video_state(void) {
    return s_state;
}

video_end_reason_t video_last_end_reason(void) {
    return s_end_reason;
}

video_quality_t video_quality(void) {
    return s_quality;
}

video_session_status_t video_session_status(void) {
    video_session_status_t status = {
        .supported = true,
        .camera_allowed = s_policy.camera_allowed != 0u,
        .microphone_allowed = s_policy.microphone_allowed != 0u,
        .call_state = s_state,
        .quality = s_quality,
        .last_end_reason = s_end_reason,
        .revision = s_policy.revision,
    };
    // A disabled policy reports an idle call with no capture permission and no
    // media state, so a stale session never reads as a live call.
    if (s_policy.enabled == 0u) {
        status.camera_allowed = false;
        status.microphone_allowed = false;
        status.call_state = VIDEO_STATE_IDLE;
        status.quality = VIDEO_QUALITY_UNKNOWN;
        status.last_end_reason = VIDEO_END_NONE;
    }
    return status;
}

static esp_err_t video_initialize(void) {
    return video_init();
}

const module_descriptor_t *video_session_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "video_session",
        .version = "1.0.0",
        .initialize = video_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef VIDEO_SESSION_HOST_TESTS
#ifdef __cplusplus
extern "C" {
#endif

void video_session_test_reset_ready(void) {
    s_ready = false;
    (void)config_store_erase_key(VIDEO_STORE_KEY);
    s_policy = video_default();
    s_state = VIDEO_STATE_IDLE;
    s_quality = VIDEO_QUALITY_UNKNOWN;
    s_end_reason = VIDEO_END_NONE;
}

#ifdef __cplusplus
}
#endif
#endif
