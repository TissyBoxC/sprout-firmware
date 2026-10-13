#include "learning_visualization_sync_core.h"

#include <cassert>
#include <cstring>

int main() {
    // Every catalog style maps to its enum; an unknown name is rejected.
    learning_style_t style = LEARNING_STYLE_CELEBRATION;
    assert(learning_visualization_sync_core_parse_style("neutral", &style));
    assert(style == LEARNING_STYLE_NEUTRAL);
    assert(learning_visualization_sync_core_parse_style("encouraging", &style));
    assert(style == LEARNING_STYLE_ENCOURAGING);
    assert(learning_visualization_sync_core_parse_style("playful", &style));
    assert(style == LEARNING_STYLE_PLAYFUL);
    assert(learning_visualization_sync_core_parse_style("celebration", &style));
    assert(style == LEARNING_STYLE_CELEBRATION);
    assert(!learning_visualization_sync_core_parse_style("loud", &style));
    assert(!learning_visualization_sync_core_parse_style(nullptr, &style));

    // Every catalog animation level maps to its enum; an unknown is rejected.
    learning_animation_level_t level = LEARNING_ANIMATION_FULL;
    assert(learning_visualization_sync_core_parse_animation("off", &level));
    assert(level == LEARNING_ANIMATION_OFF);
    assert(learning_visualization_sync_core_parse_animation("subtle", &level));
    assert(level == LEARNING_ANIMATION_SUBTLE);
    assert(learning_visualization_sync_core_parse_animation("full", &level));
    assert(level == LEARNING_ANIMATION_FULL);
    assert(!learning_visualization_sync_core_parse_animation("mega", &level));

    // A fully populated enabled policy is clamped and mapped.
    learning_visualization_remote_t remote = {};
    remote.enabled = true;
    remote.feedback_style = "encouraging";
    remote.animation_level = "full";
    remote.touch_feedback_enabled = true;
    remote.reward_effects_enabled = true;
    remote.lesson_linkage_enabled = true;
    remote.max_session_minutes = 999;
    learning_policy_t policy = {};
    assert(learning_visualization_sync_core_build_policy(&remote, &policy));
    assert(policy.schema_version == LEARNING_VISUALIZATION_SCHEMA_VERSION);
    assert(policy.enabled == 1u);
    assert(policy.feedback_style == (uint8_t)LEARNING_STYLE_ENCOURAGING);
    assert(policy.animation_level == (uint8_t)LEARNING_ANIMATION_FULL);
    assert(policy.touch_feedback_enabled == 1u);
    assert(policy.reward_effects_enabled == 1u);
    assert(policy.lesson_linkage_enabled == 1u);
    assert(policy.max_session_minutes == LEARNING_MAX_SESSION_MINUTES);
    assert(learning_is_valid(&policy));

    // A disabled policy pins the animation off.
    learning_visualization_remote_t disabled = remote;
    disabled.enabled = false;
    disabled.animation_level = "full";
    learning_policy_t disabled_policy = {};
    assert(learning_visualization_sync_core_build_policy(&disabled, &disabled_policy));
    assert(disabled_policy.enabled == 0u);
    assert(disabled_policy.animation_level == (uint8_t)LEARNING_ANIMATION_OFF);
    assert(!learning_can_visualize(&disabled_policy));

    // A negative session length floors at the minimum.
    learning_visualization_remote_t negative = remote;
    negative.max_session_minutes = -5;
    learning_policy_t negative_policy = {};
    assert(learning_visualization_sync_core_build_policy(&negative, &negative_policy));
    assert(negative_policy.max_session_minutes == LEARNING_MIN_SESSION_MINUTES);

    // An unknown style or animation rejects the whole policy.
    learning_visualization_remote_t bad_style = remote;
    bad_style.feedback_style = "loud";
    learning_policy_t bad_style_policy = {};
    assert(!learning_visualization_sync_core_build_policy(
        &bad_style,
        &bad_style_policy
    ));
    learning_visualization_remote_t bad_animation = remote;
    bad_animation.animation_level = "mega";
    learning_policy_t bad_animation_policy = {};
    assert(!learning_visualization_sync_core_build_policy(
        &bad_animation,
        &bad_animation_policy
    ));

    return 0;
}
