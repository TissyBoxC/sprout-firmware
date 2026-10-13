#pragma once

#include <stdbool.h>

#include "touch_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One bounded touch status ready to report to the platform. */
typedef struct {
    bool touch_supported;
    bool haptic_supported;
    int gesture_count;
} touch_status_t;

/**
 * @brief Build a bounded touch status from the applied configuration.
 *
 * Touch is supported only when the input layer is enabled, and haptics only
 * when the same configuration enables them, so a disabled layer never reads as
 * a usable surface. The gesture count is the number of enabled gesture classes
 * (tap, long press and swipe), so it reflects the gestures the device will
 * actually recognise. A NULL configuration yields a disabled status.
 */
touch_status_t touch_report_build(const touch_input_config_t *config);

#ifdef __cplusplus
}
#endif
