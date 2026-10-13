#pragma once

#include <stdbool.h>

#include "screen_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One bounded screen status ready to report to the platform. */
typedef struct {
    bool screen_supported;
    bool expressions_supported;
    bool animation_supported;
    bool point_read_supported;
    bool video_supported;
    screen_expression_t expression;
    screen_animation_t animation;
    screen_video_state_t video_state;
} screen_status_t;

/**
 * @brief Build a bounded screen status from the live surface state.
 *
 * Every capability flag is derived from the applied policy, so a surface that
 * disables animations, point-to-read or video never advertises them. An
 * unsupported surface reports the neutral expression, no animation and the
 * idle video state, matching the platform rule that a disabled screen must not
 * read as an active capability. A NULL policy yields the conservative default.
 */
screen_status_t screen_report_build(
    const screen_policy_t *policy,
    screen_expression_t expression,
    screen_animation_t animation,
    screen_video_state_t video_state
);

#ifdef __cplusplus
}
#endif
