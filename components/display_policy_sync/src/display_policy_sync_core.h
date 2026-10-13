#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "display_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the policy mapping is covered
 * by host tests. */

/**
 * @brief Build one applied eye-care policy from the platform response.
 *
 * Every field is clamped into the shared bounds by display_policy_clamp, so a
 * malformed or out-of-range platform value can never install an unsafe
 * brightness or screen-time cap. A negative revision is treated as zero.
 */
display_policy_t display_policy_sync_core_build(
    int brightness_percent,
    int color_temperature_kelvin,
    int min_view_distance_cm,
    int daily_limit_minutes,
    int break_interval_minutes,
    int dim_after_seconds,
    bool auto_brightness,
    bool night_light,
    bool distance_reminder,
    int revision
);

#ifdef __cplusplus
}
#endif
