#include "learning_visualization.h"

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

learning_policy_t learning_default(void) {
    const learning_policy_t policy = {
        .schema_version = LEARNING_VISUALIZATION_SCHEMA_VERSION,
        // The feature and the animation start off so the visualization is
        // opt-in; the toggles are prefilled so an enable is one step.
        .enabled = 0u,
        .feedback_style = (uint8_t)LEARNING_STYLE_NEUTRAL,
        .animation_level = (uint8_t)LEARNING_ANIMATION_OFF,
        .touch_feedback_enabled = 1u,
        .reward_effects_enabled = 1u,
        .lesson_linkage_enabled = 1u,
        .max_session_minutes = 20u,
        .reserved = 0u,
    };
    return policy;
}

learning_policy_t learning_clamp(const learning_policy_t *policy) {
    learning_policy_t clamped = learning_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.schema_version = LEARNING_VISUALIZATION_SCHEMA_VERSION;
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.feedback_style = clamp_u8(
        policy->feedback_style,
        (uint32_t)LEARNING_STYLE_NEUTRAL,
        (uint32_t)LEARNING_STYLE_CELEBRATION
    );
    clamped.animation_level = clamp_u8(
        policy->animation_level,
        (uint32_t)LEARNING_ANIMATION_OFF,
        (uint32_t)LEARNING_ANIMATION_FULL
    );
    clamped.touch_feedback_enabled =
        policy->touch_feedback_enabled != 0u ? 1u : 0u;
    clamped.reward_effects_enabled =
        policy->reward_effects_enabled != 0u ? 1u : 0u;
    clamped.lesson_linkage_enabled =
        policy->lesson_linkage_enabled != 0u ? 1u : 0u;
    clamped.max_session_minutes = clamp_u8(
        policy->max_session_minutes,
        LEARNING_MIN_SESSION_MINUTES,
        LEARNING_MAX_SESSION_MINUTES
    );
    clamped.reserved = 0u;
    // A disabled feature cannot animate; the derived level is pinned off here
    // so a stale animation setting never survives the switch.
    if (clamped.enabled == 0u) {
        clamped.animation_level = (uint8_t)LEARNING_ANIMATION_OFF;
    }
    return clamped;
}

bool learning_is_valid(const learning_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != LEARNING_VISUALIZATION_SCHEMA_VERSION) {
        return false;
    }
    if (policy->feedback_style > (uint8_t)LEARNING_STYLE_CELEBRATION) {
        return false;
    }
    if (policy->animation_level > (uint8_t)LEARNING_ANIMATION_FULL) {
        return false;
    }
    if (policy->max_session_minutes < LEARNING_MIN_SESSION_MINUTES ||
        policy->max_session_minutes > LEARNING_MAX_SESSION_MINUTES) {
        return false;
    }
    return true;
}

bool learning_can_visualize(const learning_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    const learning_policy_t bounded = learning_clamp(policy);
    return bounded.enabled != 0u;
}

bool learning_is_valid_style(learning_style_t style) {
    return style >= LEARNING_STYLE_NEUTRAL && style <= LEARNING_STYLE_CELEBRATION;
}

bool learning_is_valid_animation_level(learning_animation_level_t level) {
    return level >= LEARNING_ANIMATION_OFF && level <= LEARNING_ANIMATION_FULL;
}

bool learning_is_valid_visual_state(learning_visual_state_t state) {
    return state >= LEARNING_VISUAL_IDLE && state <= LEARNING_VISUAL_ERROR;
}

learning_animation_level_t learning_effective_animation(
    const learning_policy_t *policy,
    learning_visual_state_t state
) {
    const learning_policy_t bounded = learning_clamp(policy);
    if (bounded.enabled == 0u) {
        return LEARNING_ANIMATION_OFF;
    }
    // A paused or idle surface never animates, and a lesson only animates when
    // lesson linkage is authorised.
    switch (state) {
        case LEARNING_VISUAL_LESSON:
            if (bounded.lesson_linkage_enabled == 0u) {
                return LEARNING_ANIMATION_OFF;
            }
            return (learning_animation_level_t)bounded.animation_level;
        case LEARNING_VISUAL_FEEDBACK:
        case LEARNING_VISUAL_REWARD:
            return (learning_animation_level_t)bounded.animation_level;
        case LEARNING_VISUAL_IDLE:
        case LEARNING_VISUAL_PAUSED:
        case LEARNING_VISUAL_ERROR:
        default:
            return LEARNING_ANIMATION_OFF;
    }
}

bool learning_session_expired(
    const learning_policy_t *policy,
    uint32_t elapsed_minutes
) {
    const learning_policy_t bounded = learning_clamp(policy);
    return elapsed_minutes >= (uint32_t)bounded.max_session_minutes;
}

bool learning_lesson_id_valid(const char *lesson_id) {
    if (lesson_id == NULL) {
        return false;
    }
    const size_t length = strlen(lesson_id);
    if (length < 1 || length >= LEARNING_LESSON_ID_SIZE) {
        return false;
    }
    // A lesson id is lowercase alphanumeric with single underscores, matching
    // the platform's stable lesson identifier.
    if (lesson_id[0] < 'a' || lesson_id[0] > 'z') {
        return false;
    }
    bool previous_underscore = false;
    for (size_t index = 0; index < length; ++index) {
        const char character = lesson_id[index];
        const bool is_lower = character >= 'a' && character <= 'z';
        const bool is_digit = character >= '0' && character <= '9';
        const bool is_underscore = character == '_';
        if (!is_lower && !is_digit && !is_underscore) {
            return false;
        }
        if (is_underscore && previous_underscore) {
            return false;
        }
        previous_underscore = is_underscore;
    }
    return true;
}

const char *learning_error_name(learning_error_t error) {
    switch (error) {
        case LEARNING_VISUALIZATION_OK:
            return "ok";
        case LEARNING_VISUALIZATION_ERR_NULL_ARGUMENT:
            return "null_argument";
        case LEARNING_VISUALIZATION_ERR_INVALID_POLICY:
            return "invalid_policy";
        case LEARNING_VISUALIZATION_ERR_INVALID_STATE:
            return "invalid_state";
        case LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED:
            return "permission_denied";
        case LEARNING_VISUALIZATION_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

const char *learning_style_name(learning_style_t style) {
    switch (style) {
        case LEARNING_STYLE_NEUTRAL:
            return "neutral";
        case LEARNING_STYLE_ENCOURAGING:
            return "encouraging";
        case LEARNING_STYLE_PLAYFUL:
            return "playful";
        case LEARNING_STYLE_CELEBRATION:
            return "celebration";
        default:
            return "unknown";
    }
}

const char *learning_animation_level_name(learning_animation_level_t level) {
    switch (level) {
        case LEARNING_ANIMATION_OFF:
            return "off";
        case LEARNING_ANIMATION_SUBTLE:
            return "subtle";
        case LEARNING_ANIMATION_FULL:
            return "full";
        default:
            return "unknown";
    }
}

const char *learning_visual_state_name(learning_visual_state_t state) {
    switch (state) {
        case LEARNING_VISUAL_IDLE:
            return "idle";
        case LEARNING_VISUAL_LESSON:
            return "lesson";
        case LEARNING_VISUAL_FEEDBACK:
            return "feedback";
        case LEARNING_VISUAL_REWARD:
            return "reward";
        case LEARNING_VISUAL_PAUSED:
            return "paused";
        case LEARNING_VISUAL_ERROR:
            return "error";
        default:
            return "unknown";
    }
}
