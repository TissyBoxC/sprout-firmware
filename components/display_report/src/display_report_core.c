#include "display_report_core.h"

#include <stddef.h>

// The platform bounds the panel size at 8192 pixels per axis.
#define DISPLAY_REPORT_MAX_DIMENSION 8192

static int clamp_dimension(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > DISPLAY_REPORT_MAX_DIMENSION) {
        return DISPLAY_REPORT_MAX_DIMENSION;
    }
    return value;
}

display_status_t display_report_build(
    const display_policy_t *policy,
    int screen_width,
    int screen_height,
    bool touch_supported
) {
    display_status_t status = {
        .brightness_percent = 0,
        .screen_width = clamp_dimension(screen_width),
        .screen_height = clamp_dimension(screen_height),
        .touch_supported = touch_supported,
    };
    if (policy == NULL) {
        return status;
    }
    int brightness = (int)policy->brightness_percent;
    if (brightness < 0) {
        brightness = 0;
    }
    if (brightness > (int)DISPLAY_POLICY_MAX_BRIGHTNESS) {
        brightness = (int)DISPLAY_POLICY_MAX_BRIGHTNESS;
    }
    status.brightness_percent = brightness;
    return status;
}
