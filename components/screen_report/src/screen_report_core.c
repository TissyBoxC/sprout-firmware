#include "screen_report_core.h"

#include <stddef.h>

screen_status_t screen_report_build(
    const screen_policy_t *policy,
    screen_expression_t expression,
    screen_animation_t animation,
    screen_video_state_t video_state
) {
    screen_status_t status = {
        .screen_supported = false,
        .expressions_supported = false,
        .animation_supported = false,
        .point_read_supported = false,
        .video_supported = false,
        .expression = SCREEN_EXPRESSION_NEUTRAL,
        .animation = SCREEN_ANIMATION_NONE,
        .video_state = SCREEN_VIDEO_IDLE,
    };
    if (policy == NULL) {
        return status;
    }
    status.screen_supported = policy->enabled != 0u;
    if (!status.screen_supported) {
        return status;
    }
    status.expressions_supported = true;
    status.animation_supported = policy->animation_enabled != 0u;
    status.point_read_supported = policy->point_read_enabled != 0u;
    status.video_supported = policy->video_enabled != 0u;
    status.expression = expression;
    // A capability the surface does not advertise is reported as inactive even
    // if a stale live value was still set, so the platform view never claims an
    // animation or video the policy forbids.
    status.animation =
        status.animation_supported ? animation : SCREEN_ANIMATION_NONE;
    status.video_state =
        status.video_supported ? video_state : SCREEN_VIDEO_IDLE;
    return status;
}
