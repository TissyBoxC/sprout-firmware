#include "screen_policy_sync_core.h"

#include <string.h>

bool screen_policy_sync_core_expression_from_name(
    const char *name,
    screen_expression_t *expression_out
) {
    if (name == NULL || expression_out == NULL) {
        return false;
    }
    if (strcmp(name, "neutral") == 0) {
        *expression_out = SCREEN_EXPRESSION_NEUTRAL;
        return true;
    }
    if (strcmp(name, "happy") == 0) {
        *expression_out = SCREEN_EXPRESSION_HAPPY;
        return true;
    }
    if (strcmp(name, "sad") == 0) {
        *expression_out = SCREEN_EXPRESSION_SAD;
        return true;
    }
    if (strcmp(name, "curious") == 0) {
        *expression_out = SCREEN_EXPRESSION_CURIOUS;
        return true;
    }
    if (strcmp(name, "sleepy") == 0) {
        *expression_out = SCREEN_EXPRESSION_SLEEPY;
        return true;
    }
    if (strcmp(name, "surprised") == 0) {
        *expression_out = SCREEN_EXPRESSION_SURPRISED;
        return true;
    }
    if (strcmp(name, "excited") == 0) {
        *expression_out = SCREEN_EXPRESSION_EXCITED;
        return true;
    }
    return false;
}

bool screen_policy_sync_core_build(
    const char *default_expression,
    bool animation_enabled,
    bool point_read_enabled,
    bool video_enabled,
    int video_max_fps,
    int video_max_bitrate_kbps,
    int animation_frame_ms,
    int revision,
    screen_policy_t *policy_out
) {
    if (policy_out == NULL) {
        return false;
    }
    screen_expression_t expression = SCREEN_EXPRESSION_HAPPY;
    if (!screen_policy_sync_core_expression_from_name(
            default_expression,
            &expression
        )) {
        return false;
    }
    screen_policy_t policy = screen_default();
    policy.enabled = 1u;
    policy.default_expression = (uint8_t)expression;
    policy.animation_enabled = animation_enabled ? 1u : 0u;
    policy.point_read_enabled = point_read_enabled ? 1u : 0u;
    policy.video_enabled = video_enabled ? 1u : 0u;
    policy.video_max_fps =
        (uint8_t)(video_max_fps < 0 ? 0 : video_max_fps);
    policy.video_max_bitrate_kbps =
        (uint16_t)(video_max_bitrate_kbps < 0 ? 0 : video_max_bitrate_kbps);
    policy.animation_frame_ms =
        (uint16_t)(animation_frame_ms < 0 ? 0 : animation_frame_ms);
    policy.revision = revision < 0 ? 0u : (uint32_t)revision;
    *policy_out = screen_clamp(&policy);
    return true;
}
