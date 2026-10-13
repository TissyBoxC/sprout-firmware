#include "video_session_sync_core.h"

#include <string.h>

bool video_session_sync_core_parse_quality(
    const char *value,
    video_quality_t *quality_out
) {
    if (value == NULL || quality_out == NULL) {
        return false;
    }
    if (strcmp(value, "unknown") == 0) {
        *quality_out = VIDEO_QUALITY_UNKNOWN;
        return true;
    }
    if (strcmp(value, "good") == 0) {
        *quality_out = VIDEO_QUALITY_GOOD;
        return true;
    }
    if (strcmp(value, "fair") == 0) {
        *quality_out = VIDEO_QUALITY_FAIR;
        return true;
    }
    if (strcmp(value, "poor") == 0) {
        *quality_out = VIDEO_QUALITY_POOR;
        return true;
    }
    return false;
}

bool video_session_sync_core_build_policy(
    const video_remote_t *remote,
    video_policy_t *policy_out
) {
    if (remote == NULL || policy_out == NULL) {
        return false;
    }
    video_quality_t quality = VIDEO_QUALITY_UNKNOWN;
    if (!video_session_sync_core_parse_quality(
            remote->quality_floor,
            &quality
        )) {
        return false;
    }
    uint32_t ring_timeout = remote->ring_timeout_seconds < 0
        ? 0u
        : (uint32_t)remote->ring_timeout_seconds;
    uint32_t max_session = remote->max_session_seconds < 0
        ? 0u
        : (uint32_t)remote->max_session_seconds;
    uint32_t revision = remote->revision < 0
        ? 0u
        : (uint32_t)remote->revision;
    video_policy_t policy = video_default();
    policy.enabled = remote->enabled ? 1u : 0u;
    policy.camera_allowed = remote->camera_allowed ? 1u : 0u;
    policy.microphone_allowed = remote->microphone_allowed ? 1u : 0u;
    policy.require_child_consent = remote->require_child_consent ? 1u : 0u;
    policy.quality_floor = (uint8_t)quality;
    policy.ring_timeout_seconds = (uint16_t)ring_timeout;
    policy.max_session_seconds = (uint16_t)max_session;
    policy.revision = revision;
    // The clamp pins every timeout into its approved bounds.
    *policy_out = video_clamp(&policy);
    return true;
}
