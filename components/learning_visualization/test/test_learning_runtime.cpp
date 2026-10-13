#include "learning_visualization.h"

#include <cassert>
#include <cstring>

int main() {
    assert(learning_visualization_init() == ESP_OK);
    assert(learning_visualization_is_ready());
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_IDLE);
    assert(learning_visualization_animation_level() == LEARNING_ANIMATION_OFF);
    assert(learning_visualization_feedback_events() == 0u);
    assert(std::strcmp(learning_visualization_linked_lesson(), "") == 0);

    // A lesson is refused while the feature is disabled.
    assert(learning_visualization_begin_lesson("colors_basic") ==
           LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED);

    // Enable the feature with full animation and feedback on.
    learning_policy_t policy = learning_visualization_get();
    policy.enabled = 1u;
    policy.animation_level = (uint8_t)LEARNING_ANIMATION_FULL;
    policy.touch_feedback_enabled = 1u;
    policy.reward_effects_enabled = 1u;
    policy.lesson_linkage_enabled = 1u;
    assert(learning_visualization_set(&policy) == ESP_OK);

    // An invalid lesson id is refused before the state changes.
    assert(learning_visualization_begin_lesson("Bad_ID") ==
           ESP_ERR_INVALID_ARG);
    assert(learning_visualization_begin_lesson("colors_basic") == ESP_OK);
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_LESSON);
    assert(learning_visualization_animation_level() == LEARNING_ANIMATION_FULL);
    assert(std::strcmp(learning_visualization_linked_lesson(), "colors_basic")
           == 0);

    // Feedback is counted and enters the feedback state.
    assert(learning_visualization_report_feedback() == ESP_OK);
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_FEEDBACK);
    assert(learning_visualization_feedback_events() == 1u);

    // A reward is counted too.
    assert(learning_visualization_show_reward() == ESP_OK);
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_REWARD);
    assert(learning_visualization_feedback_events() == 2u);

    // Pausing pins the animation off; resuming restores the lesson.
    assert(learning_visualization_pause() == ESP_OK);
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_PAUSED);
    assert(learning_visualization_animation_level() == LEARNING_ANIMATION_OFF);
    assert(learning_visualization_resume() == ESP_OK);
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_LESSON);
    assert(learning_visualization_animation_level() == LEARNING_ANIMATION_FULL);

    // A disabled touch toggle refuses feedback.
    learning_policy_t no_touch = learning_visualization_get();
    no_touch.touch_feedback_enabled = 0u;
    assert(learning_visualization_set(&no_touch) == ESP_OK);
    assert(learning_visualization_report_feedback() ==
           LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED);
    no_touch.touch_feedback_enabled = 1u;
    no_touch.reward_effects_enabled = 0u;
    assert(learning_visualization_set(&no_touch) == ESP_OK);
    assert(learning_visualization_show_reward() ==
           LEARNING_VISUALIZATION_ERR_PERMISSION_DENIED);

    // An error state is a first-class bounded state.
    assert(learning_visualization_report_error() == ESP_OK);
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_ERROR);
    assert(learning_visualization_animation_level() == LEARNING_ANIMATION_OFF);

    // Stopping returns to idle and clears the linked lesson.
    assert(learning_visualization_stop() == ESP_OK);
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_IDLE);
    assert(std::strcmp(learning_visualization_linked_lesson(), "") == 0);

    // Resuming out of idle is refused.
    assert(learning_visualization_resume() ==
           LEARNING_VISUALIZATION_ERR_INVALID_STATE);

    // Disabling the feature mid-session returns the surface to idle.
    assert(learning_visualization_begin_lesson("colors_basic") == ESP_OK);
    learning_policy_t off = learning_visualization_get();
    off.enabled = 0u;
    assert(learning_visualization_set(&off) == ESP_OK);
    assert(learning_visualization_visual_state() == LEARNING_VISUAL_IDLE);
    assert(std::strcmp(learning_visualization_linked_lesson(), "") == 0);

    // The status snapshot mirrors the live surface and the policy revision.
    learning_policy_t revisioned = learning_visualization_get();
    revisioned.enabled = 1u;
    revisioned.animation_level = (uint8_t)LEARNING_ANIMATION_FULL;
    revisioned.touch_feedback_enabled = 1u;
    revisioned.revision = 8u;
    assert(learning_visualization_set(&revisioned) == ESP_OK);
    assert(learning_visualization_begin_lesson("colors_basic") == ESP_OK);
    const uint32_t events_before = learning_visualization_feedback_events();
    assert(learning_visualization_report_feedback() == ESP_OK);
    assert(learning_visualization_report_session_minutes(5000u) == ESP_OK);
    learning_visualization_status_t status = learning_visualization_status();
    assert(status.supported);
    assert(status.touch_supported);
    assert(status.visual_state == LEARNING_VISUAL_FEEDBACK);
    assert(status.animation_level == LEARNING_ANIMATION_FULL);
    assert(status.feedback_style == (learning_style_t)LEARNING_STYLE_NEUTRAL);
    assert(status.feedback_events == events_before + 1u);
    // The reported session length is clamped to the platform ceiling.
    assert(status.session_minutes == LEARNING_MAX_SESSION_MINUTES_REPORT);
    assert(std::strcmp(status.linked_lesson_id, "colors_basic") == 0);
    assert(status.revision == 8u);
    // A disabled policy reports a fully idle, animation-off surface.
    learning_policy_t off_now = learning_visualization_get();
    off_now.enabled = 0u;
    assert(learning_visualization_set(&off_now) == ESP_OK);
    learning_visualization_status_t idle = learning_visualization_status();
    assert(idle.visual_state == LEARNING_VISUAL_IDLE);
    assert(idle.animation_level == LEARNING_ANIMATION_OFF);
    assert(idle.feedback_events == 0u);
    assert(idle.session_minutes == 0u);
    assert(std::strcmp(idle.linked_lesson_id, "") == 0);

    // Persistence round trip keeps the edited policy.
    learning_policy_t persisted = learning_default();
    persisted.enabled = 1u;
    persisted.feedback_style = (uint8_t)LEARNING_STYLE_PLAYFUL;
    persisted.animation_level = (uint8_t)LEARNING_ANIMATION_SUBTLE;
    persisted.max_session_minutes = 45u;
    assert(learning_visualization_set(&persisted) == ESP_OK);
    assert(learning_visualization_init() == ESP_OK);
    assert(learning_visualization_get().enabled == 1u);
    assert(learning_visualization_get().feedback_style ==
           (uint8_t)LEARNING_STYLE_PLAYFUL);
    assert(learning_visualization_get().animation_level ==
           (uint8_t)LEARNING_ANIMATION_SUBTLE);
    assert(learning_visualization_get().max_session_minutes == 45u);

    // A policy that is not version-compatible is refused by apply.
    learning_policy_t invalid = learning_default();
    invalid.schema_version = 42u;
    assert(learning_visualization_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(learning_visualization_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor =
        learning_visualization_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
