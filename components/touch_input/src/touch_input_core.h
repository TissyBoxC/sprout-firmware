#pragma once

#include <stdint.h>

#include "touch_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief State carried between successive release events. */
typedef struct {
    uint32_t last_tap_at_ms;
    uint8_t last_tap_valid;
} touch_input_tracker_t;

/** @brief Reset a gesture tracker. */
void touch_input_tracker_init(touch_input_tracker_t *tracker);

/**
 * @brief Classify one release using the tracker's double-tap memory.
 *
 * The tracker records the release time of an accepted tap so a follow-up tap
 * inside the configured window becomes a double tap instead of a second tap.
 */
touch_gesture_t touch_input_tracker_update(
    touch_input_tracker_t *tracker,
    const touch_input_config_t *config,
    const touch_sample_t *press,
    const touch_sample_t *release
);

#ifdef __cplusplus
}
#endif
