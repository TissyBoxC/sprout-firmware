#include "screen_surface.h"

#include <cassert>
#include <cstring>

int main() {
    assert(screen_surface_init() == ESP_OK);
    assert(screen_surface_is_ready());

    const screen_policy_t defaults = screen_surface_get();
    assert(defaults.enabled == 1u);
    assert(screen_surface_current_expression() == SCREEN_EXPRESSION_HAPPY);
    assert(screen_surface_current_animation() == SCREEN_ANIMATION_NONE);
    assert(screen_surface_video_state() == SCREEN_VIDEO_IDLE);

    // Expressions.
    assert(screen_surface_show_expression(SCREEN_EXPRESSION_CURIOUS) == ESP_OK);
    assert(screen_surface_current_expression() == SCREEN_EXPRESSION_CURIOUS);
    assert(screen_surface_show_expression((screen_expression_t)200) == ESP_ERR_INVALID_ARG);

    // Animation clips advance a frame clock only while playing.
    screen_policy_t animated = defaults;
    animated.animation_frame_ms = 100u;
    animated.revision = 4u;
    assert(screen_surface_set(&animated) == ESP_OK);
    assert(screen_surface_get().revision == 4u);
    assert(screen_surface_play_animation(SCREEN_ANIMATION_WAVE) == ESP_OK);
    assert(screen_surface_current_animation() == SCREEN_ANIMATION_WAVE);
    assert(screen_surface_advance_animation(50u) == 0u);
    assert(screen_surface_advance_animation(60u) == 1u);
    assert(screen_surface_advance_animation(250u) == 3u);
    screen_surface_play_animation(SCREEN_ANIMATION_NONE);
    // Stopping a clip resets the frame clock and does not advance it.
    assert(screen_surface_current_animation() == SCREEN_ANIMATION_NONE);
    assert(screen_surface_advance_animation(1000u) == 0u);

    // Point-to-read store: put, lookup, replace, normalize and clear.
    screen_point_read_entry_t apple;
    std::strncpy(apple.term, "apple", sizeof(apple.term));
    apple.term[sizeof(apple.term) - 1u] = '\0';
    std::strncpy(apple.text, "苹果", sizeof(apple.text));
    apple.text[sizeof(apple.text) - 1u] = '\0';
    assert(screen_surface_point_read_put(&apple) == ESP_OK);
    assert(screen_surface_point_read_count() == 1u);

    screen_point_read_entry_t found;
    assert(screen_surface_point_read_lookup("  apple ", &found) == ESP_OK);
    assert(std::strcmp(found.text, "苹果") == 0);
    assert(screen_surface_point_read_lookup("banana", &found) == ESP_ERR_NOT_FOUND);

    // A duplicate term replaces its text instead of growing the store.
    screen_point_read_entry_t replaced;
    std::strncpy(replaced.term, "apple", sizeof(replaced.term));
    replaced.term[sizeof(replaced.term) - 1u] = '\0';
    std::strncpy(replaced.text, "苹果果", sizeof(replaced.text));
    replaced.text[sizeof(replaced.text) - 1u] = '\0';
    assert(screen_surface_point_read_put(&replaced) == ESP_OK);
    assert(screen_surface_point_read_count() == 1u);
    assert(screen_surface_point_read_lookup("apple", &found) == ESP_OK);
    assert(std::strcmp(found.text, "苹果果") == 0);

    // An empty term is refused.
    screen_point_read_entry_t blank;
    blank.term[0] = '\0';
    blank.text[0] = '\0';
    assert(screen_surface_point_read_put(&blank) == ESP_ERR_INVALID_ARG);

    // Disabling point-to-read fails lookup closed.
    screen_policy_t no_point_read = screen_surface_get();
    no_point_read.point_read_enabled = 0u;
    assert(screen_surface_set(&no_point_read) == ESP_OK);
    assert(screen_surface_point_read_lookup("apple", &found) == ESP_ERR_INVALID_STATE);
    assert(screen_surface_set(&animated) == ESP_OK);

    screen_surface_point_read_clear();
    assert(screen_surface_point_read_count() == 0u);

    // Video state machine.
    screen_video_plan_t plan;
    assert(screen_surface_video_begin(1200u, 24u, &plan) == ESP_OK);
    assert(plan.bitrate_kbps == 1200u);
    assert(plan.fps == 24u);
    assert(screen_surface_video_state() == SCREEN_VIDEO_LOADING);
    assert(screen_surface_video_pause() == ESP_OK);
    assert(screen_surface_video_state() == SCREEN_VIDEO_PAUSED);
    assert(screen_surface_video_resume() == ESP_OK);
    assert(screen_surface_video_state() == SCREEN_VIDEO_PLAYING);
    assert(screen_surface_video_stop() == ESP_OK);
    assert(screen_surface_video_state() == SCREEN_VIDEO_IDLE);
    assert(screen_surface_video_resume() == ESP_ERR_INVALID_STATE);
    assert(screen_surface_video_begin(1200u, 24u, nullptr) == ESP_ERR_INVALID_ARG);

    // A disabled surface refuses video and falls back to the default face.
    screen_policy_t disabled = screen_surface_get();
    disabled.enabled = 0u;
    assert(screen_surface_set(&disabled) == ESP_OK);
    assert(screen_surface_current_expression() ==
           (screen_expression_t)disabled.default_expression);
    assert(screen_surface_video_begin(1200u, 24u, &plan) == ESP_ERR_INVALID_STATE);
    assert(screen_surface_video_state() == SCREEN_VIDEO_IDLE);

    // Persistence round trip keeps an edited policy.
    screen_policy_t persisted = screen_default();
    persisted.video_max_fps = 45u;
    persisted.video_max_bitrate_kbps = 4200u;
    persisted.animation_frame_ms = 120u;
    assert(screen_surface_set(&persisted) == ESP_OK);
    assert(screen_surface_init() == ESP_OK);
    assert(screen_surface_get().video_max_fps == 45u);
    assert(screen_surface_get().video_max_bitrate_kbps == 4200u);
    assert(screen_surface_get().animation_frame_ms == 120u);
    assert(screen_surface_current_animation() == SCREEN_ANIMATION_NONE);
    assert(screen_surface_video_state() == SCREEN_VIDEO_IDLE);

    // A policy that is not version-compatible is refused by apply.
    screen_policy_t invalid;
    invalid.schema_version = 77u;
    assert(screen_surface_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(screen_surface_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = screen_surface_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
