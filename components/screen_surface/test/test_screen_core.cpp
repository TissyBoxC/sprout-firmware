#include "screen_surface.h"

#include <cassert>
#include <cstring>

int main() {
    const screen_policy_t defaults = screen_default();
    assert(defaults.schema_version == SCREEN_SURFACE_SCHEMA_VERSION);
    assert(screen_is_valid(&defaults));

    // Clamping pulls an out-of-range policy back into the bounds.
    screen_policy_t wild;
    wild.schema_version = SCREEN_SURFACE_SCHEMA_VERSION;
    wild.enabled = 1u;
    wild.default_expression = 250u;
    wild.animation_enabled = 1u;
    wild.point_read_enabled = 1u;
    wild.video_enabled = 1u;
    wild.video_max_fps = 250u;
    wild.video_max_bitrate_kbps = 65000u;
    wild.animation_frame_ms = 1u;
    const screen_policy_t clamped = screen_clamp(&wild);
    assert(clamped.default_expression == (uint8_t)SCREEN_EXPRESSION_EXCITED);
    assert(clamped.video_max_fps == SCREEN_MAX_VIDEO_FPS);
    assert(clamped.video_max_bitrate_kbps == SCREEN_MAX_VIDEO_BITRATE_KBPS);
    assert(clamped.animation_frame_ms == SCREEN_MIN_ANIMATION_FRAME_MS);
    assert(screen_is_valid(&clamped));

    // A wrong schema version is not valid.
    screen_policy_t stale = defaults;
    stale.schema_version = 42u;
    assert(!screen_is_valid(&stale));

    // Expression and animation name/validation helpers.
    assert(std::strcmp(screen_expression_name(SCREEN_EXPRESSION_HAPPY), "happy") == 0);
    assert(std::strcmp(screen_expression_name(SCREEN_EXPRESSION_NEUTRAL), "neutral") == 0);
    assert(std::strcmp(screen_animation_name(SCREEN_ANIMATION_DANCE), "dance") == 0);
    assert(std::strcmp(screen_animation_name(SCREEN_ANIMATION_NONE), "none") == 0);
    assert(std::strcmp(screen_video_state_name(SCREEN_VIDEO_PLAYING), "playing") == 0);
    assert(screen_expression_valid((uint32_t)SCREEN_EXPRESSION_EXCITED));
    assert(!screen_expression_valid((uint32_t)SCREEN_EXPRESSION_EXCITED + 1u));
    assert(screen_animation_valid((uint32_t)SCREEN_ANIMATION_DANCE));
    assert(!screen_animation_valid((uint32_t)SCREEN_ANIMATION_DANCE + 1u));
    assert(std::strcmp(screen_error_name(SCREEN_ERR_FULL), "full") == 0);

    // The video plan clamps to both the platform bounds and the policy ceiling.
    screen_policy_t modest = defaults;
    modest.video_max_bitrate_kbps = 2000u;
    modest.video_max_fps = 24u;
    const screen_video_plan_t plan = screen_video_plan(&modest, 9000u, 60u);
    assert(plan.bitrate_kbps == 2000u);
    assert(plan.fps == 24u);
    const screen_video_plan_t tiny = screen_video_plan(&modest, 1u, 1u);
    assert(tiny.bitrate_kbps == SCREEN_MIN_VIDEO_BITRATE_KBPS);
    assert(tiny.fps == SCREEN_MIN_VIDEO_FPS);

    // The frame predicate is inclusive at the configured interval.
    assert(!screen_animation_frame_due(&modest, 199u));
    assert(screen_animation_frame_due(&modest, 200u));

    // Point-to-read matches on the trimmed term and rejects an empty term.
    screen_point_read_entry_t entry;
    std::strncpy(entry.term, "apple", sizeof(entry.term));
    entry.term[sizeof(entry.term) - 1u] = '\0';
    std::strncpy(entry.text, "苹果", sizeof(entry.text));
    entry.text[sizeof(entry.text) - 1u] = '\0';
    assert(screen_point_read_matches(&entry, "  apple  "));
    assert(!screen_point_read_matches(&entry, "orange"));
    assert(!screen_point_read_matches(&entry, "   "));
    assert(!screen_point_read_matches(nullptr, "apple"));
    assert(!screen_point_read_matches(&entry, nullptr));
    return 0;
}
