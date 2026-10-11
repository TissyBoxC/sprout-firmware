#include "learning_visualization.h"

#include <cassert>
#include <cstring>

int main() {
    const learning_policy_t defaults = learning_default();
    assert(learning_is_valid(&defaults));
    assert(defaults.enabled == 0u);
    assert(defaults.animation_level == (uint8_t)LEARNING_ANIMATION_OFF);
    assert(defaults.feedback_style == (uint8_t)LEARNING_STYLE_NEUTRAL);
    assert(defaults.touch_feedback_enabled == 1u);
    assert(defaults.reward_effects_enabled == 1u);
    assert(defaults.lesson_linkage_enabled == 1u);
    assert(defaults.max_session_minutes == 20u);
    // The default policy is opt-in, so it cannot visualize yet.
    assert(!learning_can_visualize(&defaults));

    learning_policy_t out_of_range = defaults;
    out_of_range.enabled = 1u;
    out_of_range.feedback_style = 200u;
    out_of_range.animation_level = 200u;
    out_of_range.max_session_minutes = 250u;
    const learning_policy_t clamped = learning_clamp(&out_of_range);
    assert(clamped.feedback_style == (uint8_t)LEARNING_STYLE_CELEBRATION);
    assert(clamped.animation_level == (uint8_t)LEARNING_ANIMATION_FULL);
    assert(clamped.max_session_minutes == LEARNING_MAX_SESSION_MINUTES);
    assert(clamped.enabled == 1u);
    assert(learning_is_valid(&clamped));
    assert(learning_can_visualize(&clamped));

    // A zero session ceiling clamps up to the minimum.
    learning_policy_t zero_session = clamped;
    zero_session.max_session_minutes = 0u;
    assert(learning_clamp(&zero_session).max_session_minutes ==
           LEARNING_MIN_SESSION_MINUTES);

    // Disabling the feature pins the animation back off.
    learning_policy_t disabled = clamped;
    disabled.enabled = 0u;
    assert(learning_clamp(&disabled).animation_level ==
           (uint8_t)LEARNING_ANIMATION_OFF);
    assert(!learning_can_visualize(&disabled));

    assert(!learning_is_valid(nullptr));
    learning_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!learning_is_valid(&wrong_version));
    assert(learning_clamp(nullptr).max_session_minutes ==
           defaults.max_session_minutes);

    // Effective animation is off unless the surface is running.
    assert(learning_effective_animation(&clamped, LEARNING_VISUAL_IDLE) ==
           LEARNING_ANIMATION_OFF);
    assert(learning_effective_animation(&clamped, LEARNING_VISUAL_PAUSED) ==
           LEARNING_ANIMATION_OFF);
    assert(learning_effective_animation(&clamped, LEARNING_VISUAL_FEEDBACK) ==
           LEARNING_ANIMATION_FULL);
    assert(learning_effective_animation(&clamped, LEARNING_VISUAL_LESSON) ==
           LEARNING_ANIMATION_FULL);
    // A disabled lesson linkage pins the lesson animation off.
    learning_policy_t no_link = clamped;
    no_link.lesson_linkage_enabled = 0u;
    assert(learning_effective_animation(&no_link, LEARNING_VISUAL_LESSON) ==
           LEARNING_ANIMATION_OFF);
    // A disabled feature pins every state off.
    assert(learning_effective_animation(&disabled, LEARNING_VISUAL_REWARD) ==
           LEARNING_ANIMATION_OFF);

    // The session ceiling trips inclusively.
    assert(!learning_session_expired(&clamped, 59u));
    assert(learning_session_expired(&clamped, 60u));
    assert(learning_session_expired(&clamped, 61u));

    // Lesson ids are bounded lowercase identifiers.
    assert(learning_lesson_id_valid("colors_basic"));
    assert(!learning_lesson_id_valid(""));
    assert(!learning_lesson_id_valid("Colors_basic"));
    assert(!learning_lesson_id_valid("colors__basic"));
    assert(!learning_lesson_id_valid("colors-basic"));
    assert(!learning_lesson_id_valid(nullptr));

    // Catalog membership is bounded on both ends.
    assert(learning_is_valid_style(LEARNING_STYLE_NEUTRAL));
    assert(learning_is_valid_style(LEARNING_STYLE_CELEBRATION));
    assert(!learning_is_valid_style((learning_style_t)99));
    assert(learning_is_valid_animation_level(LEARNING_ANIMATION_OFF));
    assert(learning_is_valid_animation_level(LEARNING_ANIMATION_FULL));
    assert(!learning_is_valid_animation_level(
        (learning_animation_level_t)99
    ));
    assert(learning_is_valid_visual_state(LEARNING_VISUAL_IDLE));
    assert(learning_is_valid_visual_state(LEARNING_VISUAL_ERROR));
    assert(!learning_is_valid_visual_state((learning_visual_state_t)99));

    // Stable names round-trip for the platform contract.
    assert(std::strcmp(learning_style_name(LEARNING_STYLE_CELEBRATION),
                       "celebration") == 0);
    assert(std::strcmp(
               learning_animation_level_name(LEARNING_ANIMATION_SUBTLE),
               "subtle"
           ) == 0);
    assert(std::strcmp(learning_visual_state_name(LEARNING_VISUAL_PAUSED),
                       "paused") == 0);
    return 0;
}
