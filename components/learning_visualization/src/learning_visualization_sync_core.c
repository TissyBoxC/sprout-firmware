#include "learning_visualization_sync_core.h"

#include <string.h>

bool learning_visualization_sync_core_parse_style(
    const char *value,
    learning_style_t *style_out
) {
    if (value == NULL || style_out == NULL) {
        return false;
    }
    if (strcmp(value, "neutral") == 0) {
        *style_out = LEARNING_STYLE_NEUTRAL;
        return true;
    }
    if (strcmp(value, "encouraging") == 0) {
        *style_out = LEARNING_STYLE_ENCOURAGING;
        return true;
    }
    if (strcmp(value, "playful") == 0) {
        *style_out = LEARNING_STYLE_PLAYFUL;
        return true;
    }
    if (strcmp(value, "celebration") == 0) {
        *style_out = LEARNING_STYLE_CELEBRATION;
        return true;
    }
    return false;
}

bool learning_visualization_sync_core_parse_animation(
    const char *value,
    learning_animation_level_t *level_out
) {
    if (value == NULL || level_out == NULL) {
        return false;
    }
    if (strcmp(value, "off") == 0) {
        *level_out = LEARNING_ANIMATION_OFF;
        return true;
    }
    if (strcmp(value, "subtle") == 0) {
        *level_out = LEARNING_ANIMATION_SUBTLE;
        return true;
    }
    if (strcmp(value, "full") == 0) {
        *level_out = LEARNING_ANIMATION_FULL;
        return true;
    }
    return false;
}

bool learning_visualization_sync_core_build_policy(
    const learning_visualization_remote_t *remote,
    learning_policy_t *policy_out
) {
    if (remote == NULL || policy_out == NULL) {
        return false;
    }
    learning_style_t style = LEARNING_STYLE_NEUTRAL;
    if (!learning_visualization_sync_core_parse_style(
            remote->feedback_style,
            &style
        )) {
        return false;
    }
    learning_animation_level_t animation = LEARNING_ANIMATION_OFF;
    if (!learning_visualization_sync_core_parse_animation(
            remote->animation_level,
            &animation
        )) {
        return false;
    }
    uint32_t session_minutes = remote->max_session_minutes < 0
        ? 0u
        : (uint32_t)remote->max_session_minutes;
    learning_policy_t policy = learning_default();
    policy.enabled = remote->enabled ? 1u : 0u;
    policy.feedback_style = (uint8_t)style;
    policy.animation_level = (uint8_t)animation;
    policy.touch_feedback_enabled = remote->touch_feedback_enabled ? 1u : 0u;
    policy.reward_effects_enabled = remote->reward_effects_enabled ? 1u : 0u;
    policy.lesson_linkage_enabled = remote->lesson_linkage_enabled ? 1u : 0u;
    policy.max_session_minutes = (uint8_t)session_minutes;
    // The clamp pins the animation off when the feature is disabled.
    *policy_out = learning_clamp(&policy);
    return true;
}
