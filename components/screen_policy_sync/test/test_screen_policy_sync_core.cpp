#include "screen_policy_sync_core.h"

#include <cassert>
#include <cstring>

int main() {
    // A well-formed platform policy maps onto the firmware contract.
    screen_policy_t policy = {};
    assert(screen_policy_sync_core_build(
        "curious",
        true,
        true,
        false,
        45,
        4000,
        120,
        6,
        &policy
    ));
    assert(policy.schema_version == SCREEN_SURFACE_SCHEMA_VERSION);
    assert(policy.default_expression == SCREEN_EXPRESSION_CURIOUS);
    assert(policy.animation_enabled == 1u);
    assert(policy.point_read_enabled == 1u);
    assert(policy.video_enabled == 0u);
    assert(policy.video_max_fps == 45u);
    assert(policy.video_max_bitrate_kbps == 4000u);
    assert(policy.animation_frame_ms == 120u);
    assert(policy.revision == 6u);

    // Out-of-range values are clamped instead of installed verbatim.
    screen_policy_t clamped = {};
    assert(screen_policy_sync_core_build(
        "neutral",
        false,
        false,
        true,
        999,
        1,
        1,
        -4,
        &clamped
    ));
    assert(clamped.video_max_fps == SCREEN_MAX_VIDEO_FPS);
    assert(clamped.video_max_bitrate_kbps == SCREEN_MIN_VIDEO_BITRATE_KBPS);
    assert(clamped.animation_frame_ms == SCREEN_MIN_ANIMATION_FRAME_MS);
    assert(clamped.revision == 0u);
    assert(screen_is_valid(&clamped));

    // An unknown expression or null output is rejected.
    screen_policy_t rejected = {};
    assert(!screen_policy_sync_core_build(
        "furious", true, true, true, 30, 1500, 200, 1, &rejected
    ));
    assert(!screen_policy_sync_core_build(
        "happy", true, true, true, 30, 1500, 200, 1, nullptr
    ));

    // The shared expression catalog round-trips through the parser.
    screen_expression_t expression = SCREEN_EXPRESSION_NEUTRAL;
    assert(screen_policy_sync_core_expression_from_name("excited", &expression));
    assert(expression == SCREEN_EXPRESSION_EXCITED);
    assert(!screen_policy_sync_core_expression_from_name("dance", &expression));

    return 0;
}
