#include "touch_report_core.h"

#include <stddef.h>

touch_status_t touch_report_build(const touch_input_config_t *config) {
    touch_status_t status = {
        .touch_supported = false,
        .haptic_supported = false,
        .gesture_count = 0,
    };
    if (config == NULL || config->enabled == 0u) {
        return status;
    }
    status.touch_supported = true;
    status.haptic_supported = config->haptic_enabled != 0u;
    // Tap covers tap and double tap, so the enabled classes are counted rather
    // than the raw gesture enum.
    if (config->tap_enabled != 0u) {
        status.gesture_count++;
    }
    if (config->long_press_enabled != 0u) {
        status.gesture_count++;
    }
    if (config->swipe_enabled != 0u) {
        status.gesture_count++;
    }
    return status;
}
