#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "touch_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the config mapping is covered
 * by host tests. */

/**
 * @brief Build one applied touch configuration from the platform response.
 *
 * Every field is clamped into the shared bounds by touch_input_clamp, so a
 * malformed or out-of-range platform value can never install an unsafe
 * sensitivity, rotation or gesture window. A negative revision is treated as
 * zero.
 */
touch_input_config_t touch_input_sync_core_build(
    bool enabled,
    bool tap_enabled,
    bool swipe_enabled,
    bool long_press_enabled,
    bool haptic_enabled,
    int sensitivity,
    int rotation_degrees,
    int long_press_ms,
    int double_tap_ms,
    int swipe_distance_px,
    int revision
);

#ifdef __cplusplus
}
#endif
