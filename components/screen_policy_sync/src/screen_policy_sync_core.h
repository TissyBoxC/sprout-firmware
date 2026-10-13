#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "screen_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the policy mapping is covered
 * by host tests. */

/** @brief Map the platform expression name onto the firmware enum. */
bool screen_policy_sync_core_expression_from_name(
    const char *name,
    screen_expression_t *expression_out
);

/**
 * @brief Build one applied screen policy from the platform response.
 *
 * The default expression is resolved through the shared name catalog and every
 * field is clamped by screen_clamp, so a malformed or out-of-range platform
 * value can never install an unsupported frame rate or video profile. A
 * negative revision is treated as zero.
 */
bool screen_policy_sync_core_build(
    const char *default_expression,
    bool animation_enabled,
    bool point_read_enabled,
    bool video_enabled,
    int video_max_fps,
    int video_max_bitrate_kbps,
    int animation_frame_ms,
    int revision,
    screen_policy_t *policy_out
);

#ifdef __cplusplus
}
#endif
