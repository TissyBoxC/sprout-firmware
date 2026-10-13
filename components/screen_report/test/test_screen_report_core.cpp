#include "screen_report_core.h"

#include <cassert>

int main() {
    // A fully enabled surface advertises every capability and reports the live
    // expression, animation and video state.
    screen_policy_t enabled = screen_default();
    enabled.enabled = 1u;
    enabled.animation_enabled = 1u;
    enabled.point_read_enabled = 1u;
    enabled.video_enabled = 1u;
    screen_status_t active = screen_report_build(
        &enabled,
        SCREEN_EXPRESSION_HAPPY,
        SCREEN_ANIMATION_WAVE,
        SCREEN_VIDEO_PLAYING
    );
    assert(active.screen_supported);
    assert(active.expressions_supported);
    assert(active.animation_supported);
    assert(active.point_read_supported);
    assert(active.video_supported);
    assert(active.expression == SCREEN_EXPRESSION_HAPPY);
    assert(active.animation == SCREEN_ANIMATION_WAVE);
    assert(active.video_state == SCREEN_VIDEO_PLAYING);

    // A disabled surface reports no capability and the neutral inactive state,
    // so a stale live value never reads as an active capability.
    screen_policy_t disabled = screen_default();
    disabled.enabled = 0u;
    screen_status_t off = screen_report_build(
        &disabled,
        SCREEN_EXPRESSION_EXCITED,
        SCREEN_ANIMATION_DANCE,
        SCREEN_VIDEO_PLAYING
    );
    assert(off.screen_supported == false);
    assert(off.expressions_supported == false);
    assert(off.animation_supported == false);
    assert(off.point_read_supported == false);
    assert(off.video_supported == false);
    assert(off.expression == SCREEN_EXPRESSION_NEUTRAL);
    assert(off.animation == SCREEN_ANIMATION_NONE);
    assert(off.video_state == SCREEN_VIDEO_IDLE);

    // An enabled surface can still disable individual capabilities; a forbidden
    // animation or video is reported as inactive even when set locally.
    screen_policy_t limited = screen_default();
    limited.enabled = 1u;
    limited.animation_enabled = 0u;
    limited.point_read_enabled = 0u;
    limited.video_enabled = 0u;
    screen_status_t limited_status = screen_report_build(
        &limited,
        SCREEN_EXPRESSION_CURIOUS,
        SCREEN_ANIMATION_NOD,
        SCREEN_VIDEO_PAUSED
    );
    assert(limited_status.screen_supported);
    assert(limited_status.expressions_supported);
    assert(limited_status.animation_supported == false);
    assert(limited_status.point_read_supported == false);
    assert(limited_status.video_supported == false);
    assert(limited_status.expression == SCREEN_EXPRESSION_CURIOUS);
    assert(limited_status.animation == SCREEN_ANIMATION_NONE);
    assert(limited_status.video_state == SCREEN_VIDEO_IDLE);

    // A NULL policy yields the conservative default.
    screen_status_t null_status = screen_report_build(
        nullptr,
        SCREEN_EXPRESSION_HAPPY,
        SCREEN_ANIMATION_IDLE,
        SCREEN_VIDEO_LOADING
    );
    assert(null_status.screen_supported == false);
    assert(null_status.expression == SCREEN_EXPRESSION_NEUTRAL);
    assert(null_status.animation == SCREEN_ANIMATION_NONE);
    assert(null_status.video_state == SCREEN_VIDEO_IDLE);

    return 0;
}
