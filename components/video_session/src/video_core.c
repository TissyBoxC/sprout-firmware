#include "video_session.h"

#include <stddef.h>

static uint8_t clamp_u8(uint32_t value, uint32_t minimum, uint32_t maximum) {
    if (value < minimum) {
        return (uint8_t)minimum;
    }
    if (value > maximum) {
        return (uint8_t)maximum;
    }
    return (uint8_t)value;
}

static uint16_t clamp_u16(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    if (value < minimum) {
        return (uint16_t)minimum;
    }
    if (value > maximum) {
        return (uint16_t)maximum;
    }
    return (uint16_t)value;
}

video_policy_t video_default(void) {
    const video_policy_t policy = {
        .schema_version = VIDEO_SESSION_SCHEMA_VERSION,
        .enabled = 1u,
        // The default is a privacy-first session: no capture until the guardian
        // and child consent and the permissions are enabled.
        .camera_allowed = 0u,
        .microphone_allowed = 0u,
        .require_child_consent = 1u,
        .quality_floor = (uint8_t)VIDEO_QUALITY_POOR,
        .reserved = 0u,
        .ring_timeout_seconds = 30u,
        .max_session_seconds = 1800u,
    };
    return policy;
}

video_policy_t video_clamp(const video_policy_t *policy) {
    video_policy_t clamped = video_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.schema_version = VIDEO_SESSION_SCHEMA_VERSION;
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.camera_allowed = policy->camera_allowed != 0u ? 1u : 0u;
    clamped.microphone_allowed = policy->microphone_allowed != 0u ? 1u : 0u;
    clamped.require_child_consent =
        policy->require_child_consent != 0u ? 1u : 0u;
    clamped.quality_floor = clamp_u8(
        policy->quality_floor,
        (uint32_t)VIDEO_QUALITY_UNKNOWN,
        (uint32_t)VIDEO_QUALITY_POOR
    );
    clamped.reserved = 0u;
    clamped.ring_timeout_seconds = clamp_u16(
        policy->ring_timeout_seconds,
        VIDEO_MIN_RING_TIMEOUT_SECONDS,
        VIDEO_MAX_RING_TIMEOUT_SECONDS
    );
    clamped.max_session_seconds = clamp_u16(
        policy->max_session_seconds,
        VIDEO_MIN_MAX_SESSION_SECONDS,
        VIDEO_MAX_MAX_SESSION_SECONDS
    );
    return clamped;
}

bool video_is_valid(const video_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != VIDEO_SESSION_SCHEMA_VERSION) {
        return false;
    }
    if (policy->quality_floor > (uint8_t)VIDEO_QUALITY_POOR) {
        return false;
    }
    if (policy->ring_timeout_seconds < VIDEO_MIN_RING_TIMEOUT_SECONDS ||
        policy->ring_timeout_seconds > VIDEO_MAX_RING_TIMEOUT_SECONDS) {
        return false;
    }
    if (policy->max_session_seconds < VIDEO_MIN_MAX_SESSION_SECONDS ||
        policy->max_session_seconds > VIDEO_MAX_MAX_SESSION_SECONDS) {
        return false;
    }
    return true;
}

bool video_can_capture(const video_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    const video_policy_t bounded = video_clamp(policy);
    if (bounded.enabled == 0u) {
        return false;
    }
    if (bounded.microphone_allowed == 0u && bounded.camera_allowed == 0u) {
        return false;
    }
    return true;
}

bool video_ring_timed_out(
    const video_policy_t *policy,
    uint32_t elapsed_ms
) {
    const video_policy_t bounded = video_clamp(policy);
    const uint32_t limit_ms =
        (uint32_t)bounded.ring_timeout_seconds * 1000u;
    return elapsed_ms >= limit_ms;
}

const char *video_error_name(video_error_t error) {
    switch (error) {
        case VIDEO_OK:
            return "ok";
        case VIDEO_ERR_NULL_ARGUMENT:
            return "null_argument";
        case VIDEO_ERR_INVALID_POLICY:
            return "invalid_policy";
        case VIDEO_ERR_INVALID_STATE:
            return "invalid_state";
        case VIDEO_ERR_PERMISSION_DENIED:
            return "permission_denied";
        case VIDEO_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

const char *video_state_name(video_state_t state) {
    switch (state) {
        case VIDEO_STATE_RINGING:
            return "ringing";
        case VIDEO_STATE_ACTIVE:
            return "active";
        case VIDEO_STATE_ENDED:
            return "ended";
        case VIDEO_STATE_REJECTED:
            return "rejected";
        case VIDEO_STATE_FAILED:
            return "failed";
        case VIDEO_STATE_IDLE:
        default:
            return "idle";
    }
}

const char *video_quality_name(video_quality_t quality) {
    switch (quality) {
        case VIDEO_QUALITY_GOOD:
            return "good";
        case VIDEO_QUALITY_FAIR:
            return "fair";
        case VIDEO_QUALITY_POOR:
            return "poor";
        case VIDEO_QUALITY_UNKNOWN:
        default:
            return "unknown";
    }
}

const char *video_end_reason_name(video_end_reason_t reason) {
    switch (reason) {
        case VIDEO_END_HANGUP:
            return "hangup";
        case VIDEO_END_REJECTED:
            return "rejected";
        case VIDEO_END_TIMEOUT:
            return "timeout";
        case VIDEO_END_NETWORK:
            return "network";
        case VIDEO_END_PERMISSION_REVOKED:
            return "permission_revoked";
        case VIDEO_END_NONE:
        default:
            return "none";
    }
}
