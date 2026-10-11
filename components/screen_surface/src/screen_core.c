#include "screen_surface.h"

#include <stddef.h>
#include <string.h>

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

// Trim leading and trailing spaces into a stable, always-terminated buffer.
static void trim_spaces(const char *source, char *destination, size_t capacity) {
    if (destination == NULL || capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t start = 0u;
    while (source[start] == ' ') {
        start++;
    }
    size_t end = start;
    while (source[end] != '\0') {
        end++;
    }
    while (end > start && source[end - 1u] == ' ') {
        end--;
    }
    const size_t length = end - start;
    const size_t written = length < capacity - 1u ? length : capacity - 1u;
    for (size_t index = 0u; index < written; index++) {
        destination[index] = source[start + index];
    }
    destination[written] = '\0';
}

screen_policy_t screen_default(void) {
    const screen_policy_t policy = {
        .schema_version = SCREEN_SURFACE_SCHEMA_VERSION,
        .enabled = 1u,
        .default_expression = (uint8_t)SCREEN_EXPRESSION_HAPPY,
        .animation_enabled = 1u,
        .point_read_enabled = 1u,
        .video_enabled = 1u,
        .video_max_fps = 30u,
        // A low default bitrate keeps a slow link playable instead of failing
        // outright; the guardian can raise it within the documented bounds.
        .video_max_bitrate_kbps = 1500u,
        .animation_frame_ms = 200u,
    };
    return policy;
}

screen_policy_t screen_clamp(const screen_policy_t *policy) {
    screen_policy_t clamped = screen_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.schema_version = SCREEN_SURFACE_SCHEMA_VERSION;
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.default_expression = clamp_u8(
        policy->default_expression,
        (uint32_t)SCREEN_EXPRESSION_NEUTRAL,
        (uint32_t)SCREEN_EXPRESSION_EXCITED
    );
    clamped.animation_enabled = policy->animation_enabled != 0u ? 1u : 0u;
    clamped.point_read_enabled = policy->point_read_enabled != 0u ? 1u : 0u;
    clamped.video_enabled = policy->video_enabled != 0u ? 1u : 0u;
    clamped.video_max_fps = clamp_u8(
        policy->video_max_fps,
        SCREEN_MIN_VIDEO_FPS,
        SCREEN_MAX_VIDEO_FPS
    );
    clamped.video_max_bitrate_kbps = clamp_u16(
        policy->video_max_bitrate_kbps,
        SCREEN_MIN_VIDEO_BITRATE_KBPS,
        SCREEN_MAX_VIDEO_BITRATE_KBPS
    );
    clamped.animation_frame_ms = clamp_u16(
        policy->animation_frame_ms,
        SCREEN_MIN_ANIMATION_FRAME_MS,
        SCREEN_MAX_ANIMATION_FRAME_MS
    );
    return clamped;
}

bool screen_is_valid(const screen_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != SCREEN_SURFACE_SCHEMA_VERSION) {
        return false;
    }
    if (!screen_expression_valid(policy->default_expression)) {
        return false;
    }
    const uint32_t max_fps = policy->video_max_fps;
    if (max_fps < SCREEN_MIN_VIDEO_FPS || max_fps > SCREEN_MAX_VIDEO_FPS) {
        return false;
    }
    if (policy->video_max_bitrate_kbps < SCREEN_MIN_VIDEO_BITRATE_KBPS ||
        policy->video_max_bitrate_kbps > SCREEN_MAX_VIDEO_BITRATE_KBPS) {
        return false;
    }
    if (policy->animation_frame_ms < SCREEN_MIN_ANIMATION_FRAME_MS ||
        policy->animation_frame_ms > SCREEN_MAX_ANIMATION_FRAME_MS) {
        return false;
    }
    return true;
}

const char *screen_error_name(screen_error_t error) {
    switch (error) {
        case SCREEN_OK:
            return "ok";
        case SCREEN_ERR_NULL_ARGUMENT:
            return "null_argument";
        case SCREEN_ERR_INVALID_POLICY:
            return "invalid_policy";
        case SCREEN_ERR_INVALID_EXPRESSION:
            return "invalid_expression";
        case SCREEN_ERR_INVALID_ANIMATION:
            return "invalid_animation";
        case SCREEN_ERR_INVALID_STATE:
            return "invalid_state";
        case SCREEN_ERR_STORAGE:
            return "storage";
        case SCREEN_ERR_NOT_FOUND:
            return "not_found";
        case SCREEN_ERR_FULL:
            return "full";
        default:
            return "unknown";
    }
}

const char *screen_expression_name(screen_expression_t expression) {
    switch (expression) {
        case SCREEN_EXPRESSION_HAPPY:
            return "happy";
        case SCREEN_EXPRESSION_SAD:
            return "sad";
        case SCREEN_EXPRESSION_CURIOUS:
            return "curious";
        case SCREEN_EXPRESSION_SLEEPY:
            return "sleepy";
        case SCREEN_EXPRESSION_SURPRISED:
            return "surprised";
        case SCREEN_EXPRESSION_EXCITED:
            return "excited";
        case SCREEN_EXPRESSION_NEUTRAL:
        default:
            return "neutral";
    }
}

const char *screen_animation_name(screen_animation_t animation) {
    switch (animation) {
        case SCREEN_ANIMATION_IDLE:
            return "idle";
        case SCREEN_ANIMATION_BLINK:
            return "blink";
        case SCREEN_ANIMATION_WAVE:
            return "wave";
        case SCREEN_ANIMATION_NOD:
            return "nod";
        case SCREEN_ANIMATION_DANCE:
            return "dance";
        case SCREEN_ANIMATION_NONE:
        default:
            return "none";
    }
}

const char *screen_video_state_name(screen_video_state_t state) {
    switch (state) {
        case SCREEN_VIDEO_LOADING:
            return "loading";
        case SCREEN_VIDEO_PLAYING:
            return "playing";
        case SCREEN_VIDEO_PAUSED:
            return "paused";
        case SCREEN_VIDEO_ENDED:
            return "ended";
        case SCREEN_VIDEO_FAILED:
            return "failed";
        case SCREEN_VIDEO_IDLE:
        default:
            return "idle";
    }
}

bool screen_expression_valid(uint32_t expression) {
    return expression <= (uint32_t)SCREEN_EXPRESSION_EXCITED;
}

bool screen_animation_valid(uint32_t animation) {
    return animation <= (uint32_t)SCREEN_ANIMATION_DANCE;
}

screen_video_plan_t screen_video_plan(
    const screen_policy_t *policy,
    uint32_t requested_bitrate_kbps,
    uint32_t requested_fps
) {
    const screen_policy_t bounded = screen_clamp(policy);
    screen_video_plan_t plan;
    plan.bitrate_kbps = clamp_u16(
        requested_bitrate_kbps,
        SCREEN_MIN_VIDEO_BITRATE_KBPS,
        bounded.video_max_bitrate_kbps
    );
    plan.fps = clamp_u8(
        requested_fps,
        SCREEN_MIN_VIDEO_FPS,
        bounded.video_max_fps
    );
    return plan;
}

bool screen_animation_frame_due(
    const screen_policy_t *policy,
    uint32_t elapsed_ms
) {
    const screen_policy_t bounded = screen_clamp(policy);
    return elapsed_ms >= bounded.animation_frame_ms;
}

bool screen_point_read_matches(
    const screen_point_read_entry_t *entry,
    const char *term
) {
    if (entry == NULL || term == NULL) {
        return false;
    }
    char normalized_term[SCREEN_POINT_READ_MAX_TERM + 1];
    trim_spaces(term, normalized_term, sizeof(normalized_term));
    if (normalized_term[0] == '\0') {
        return false;
    }
    return strcmp(entry->term, normalized_term) == 0;
}

