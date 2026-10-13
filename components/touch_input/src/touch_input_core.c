#include "touch_input_core.h"

#include <stddef.h>
#include <string.h>

static uint8_t clamp_u8(uint32_t value, uint32_t minimum, uint32_t maximum) {
    if (value < minimum) {
        return (uint8_t)minimum;
    }
    if (value > maximum) {
        return (uint8_t)maximum;
    }
    return (uint8_t)value;
}

static uint16_t clamp_u16(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    if (value < minimum) {
        return (uint16_t)minimum;
    }
    if (value > maximum) {
        return (uint16_t)maximum;
    }
    return (uint16_t)value;
}

static bool rotation_supported(uint16_t rotation_degrees) {
    switch (rotation_degrees) {
        case TOUCH_INPUT_ROTATION_0:
        case TOUCH_INPUT_ROTATION_90:
        case TOUCH_INPUT_ROTATION_180:
        case TOUCH_INPUT_ROTATION_270:
            return true;
        default:
            return false;
    }
}

touch_input_config_t touch_input_default(void) {
    const touch_input_config_t config = {
        .schema_version = TOUCH_INPUT_SCHEMA_VERSION,
        .enabled = 1u,
        .tap_enabled = 1u,
        .swipe_enabled = 1u,
        .long_press_enabled = 1u,
        .haptic_enabled = 1u,
        .sensitivity = 60u,
        .rotation_degrees = TOUCH_INPUT_ROTATION_0,
        .long_press_ms = 700u,
        .double_tap_ms = 300u,
        .swipe_distance_px = 60u,
        .revision = 0u,
    };
    return config;
}

touch_input_config_t touch_input_clamp(const touch_input_config_t *config) {
    touch_input_config_t clamped = touch_input_default();
    if (config == NULL) {
        return clamped;
    }
    clamped.enabled = config->enabled != 0u ? 1u : 0u;
    clamped.tap_enabled = config->tap_enabled != 0u ? 1u : 0u;
    clamped.swipe_enabled = config->swipe_enabled != 0u ? 1u : 0u;
    clamped.long_press_enabled = config->long_press_enabled != 0u ? 1u : 0u;
    clamped.haptic_enabled = config->haptic_enabled != 0u ? 1u : 0u;
    clamped.sensitivity = clamp_u8(
        config->sensitivity,
        TOUCH_INPUT_MIN_SENSITIVITY,
        TOUCH_INPUT_MAX_SENSITIVITY
    );
    clamped.rotation_degrees = rotation_supported(config->rotation_degrees)
        ? config->rotation_degrees
        : TOUCH_INPUT_ROTATION_0;
    clamped.long_press_ms = clamp_u16(
        config->long_press_ms,
        TOUCH_INPUT_MIN_LONG_PRESS_MS,
        TOUCH_INPUT_MAX_LONG_PRESS_MS
    );
    clamped.double_tap_ms = clamp_u16(
        config->double_tap_ms,
        TOUCH_INPUT_MIN_DOUBLE_TAP_MS,
        TOUCH_INPUT_MAX_DOUBLE_TAP_MS
    );
    clamped.swipe_distance_px = clamp_u16(
        config->swipe_distance_px,
        TOUCH_INPUT_MIN_SWIPE_DISTANCE_PX,
        TOUCH_INPUT_MAX_SWIPE_DISTANCE_PX
    );
    clamped.revision = config->revision;
    clamped.schema_version = TOUCH_INPUT_SCHEMA_VERSION;
    return clamped;
}

bool touch_input_is_valid(const touch_input_config_t *config) {
    if (config == NULL) {
        return false;
    }
    if (config->schema_version != TOUCH_INPUT_SCHEMA_VERSION) {
        return false;
    }
    if (config->sensitivity < TOUCH_INPUT_MIN_SENSITIVITY ||
        config->sensitivity > TOUCH_INPUT_MAX_SENSITIVITY) {
        return false;
    }
    if (!rotation_supported(config->rotation_degrees)) {
        return false;
    }
    if (config->long_press_ms < TOUCH_INPUT_MIN_LONG_PRESS_MS ||
        config->long_press_ms > TOUCH_INPUT_MAX_LONG_PRESS_MS) {
        return false;
    }
    if (config->double_tap_ms < TOUCH_INPUT_MIN_DOUBLE_TAP_MS ||
        config->double_tap_ms > TOUCH_INPUT_MAX_DOUBLE_TAP_MS) {
        return false;
    }
    if (config->swipe_distance_px < TOUCH_INPUT_MIN_SWIPE_DISTANCE_PX ||
        config->swipe_distance_px > TOUCH_INPUT_MAX_SWIPE_DISTANCE_PX) {
        return false;
    }
    return true;
}

bool touch_input_gesture_enabled(
    const touch_input_config_t *config,
    touch_gesture_t gesture
) {
    if (config == NULL || config->enabled == 0u) {
        return false;
    }
    switch (gesture) {
        case TOUCH_GESTURE_TAP:
        case TOUCH_GESTURE_DOUBLE_TAP:
            return config->tap_enabled != 0u;
        case TOUCH_GESTURE_LONG_PRESS:
            return config->long_press_enabled != 0u;
        case TOUCH_GESTURE_SWIPE_LEFT:
        case TOUCH_GESTURE_SWIPE_RIGHT:
        case TOUCH_GESTURE_SWIPE_UP:
        case TOUCH_GESTURE_SWIPE_DOWN:
            return config->swipe_enabled != 0u;
        default:
            return false;
    }
}

static uint32_t distance_squared(
    const touch_sample_t *first,
    const touch_sample_t *second
) {
    const int32_t dx = (int32_t)second->x - (int32_t)first->x;
    const int32_t dy = (int32_t)second->y - (int32_t)first->y;
    return (uint32_t)(dx * dx + dy * dy);
}

static touch_gesture_t classify_swipe(
    const touch_input_config_t *config,
    const touch_sample_t *press,
    const touch_sample_t *release
) {
    const int32_t dx = (int32_t)release->x - (int32_t)press->x;
    const int32_t dy = (int32_t)release->y - (int32_t)press->y;
    const int32_t abs_dx = dx < 0 ? -dx : dx;
    const int32_t abs_dy = dy < 0 ? -dy : dy;
    if (abs_dx >= (int32_t)config->swipe_distance_px &&
        abs_dx >= abs_dy) {
        return dx > 0 ? TOUCH_GESTURE_SWIPE_RIGHT : TOUCH_GESTURE_SWIPE_LEFT;
    }
    if (abs_dy >= (int32_t)config->swipe_distance_px &&
        abs_dy > abs_dx) {
        // Screen coordinates grow downward, so a positive dy is a downward
        // swipe.
        return dy > 0 ? TOUCH_GESTURE_SWIPE_DOWN : TOUCH_GESTURE_SWIPE_UP;
    }
    return TOUCH_GESTURE_NONE;
}

touch_gesture_t touch_input_classify_release(
    const touch_input_config_t *config,
    const touch_sample_t *press,
    const touch_sample_t *release
) {
    if (config == NULL || press == NULL || release == NULL) {
        return TOUCH_GESTURE_NONE;
    }
    const touch_input_config_t bounded = touch_input_clamp(config);
    if (bounded.enabled == 0u) {
        return TOUCH_GESTURE_NONE;
    }
    if (release->timestamp_ms < press->timestamp_ms) {
        return TOUCH_GESTURE_NONE;
    }
    const uint32_t held_ms =
        release->timestamp_ms - press->timestamp_ms;
    const uint32_t moved_squared = distance_squared(press, release);
    const uint32_t swipe_threshold_squared =
        (uint32_t)bounded.swipe_distance_px *
        (uint32_t)bounded.swipe_distance_px;
    if (moved_squared >= swipe_threshold_squared) {
        const touch_gesture_t swipe = classify_swipe(config, press, release);
        if (swipe == TOUCH_GESTURE_NONE) {
            return TOUCH_GESTURE_NONE;
        }
        return touch_input_gesture_enabled(config, swipe)
            ? swipe
            : TOUCH_GESTURE_NONE;
    }
    if (held_ms >= (uint32_t)bounded.long_press_ms) {
        return touch_input_gesture_enabled(config, TOUCH_GESTURE_LONG_PRESS)
            ? TOUCH_GESTURE_LONG_PRESS
            : TOUCH_GESTURE_NONE;
    }
    return touch_input_gesture_enabled(config, TOUCH_GESTURE_TAP)
        ? TOUCH_GESTURE_TAP
        : TOUCH_GESTURE_NONE;
}

void touch_input_tracker_init(touch_input_tracker_t *tracker) {
    if (tracker == NULL) {
        return;
    }
    memset(tracker, 0, sizeof(*tracker));
}

touch_gesture_t touch_input_tracker_update(
    touch_input_tracker_t *tracker,
    const touch_input_config_t *config,
    const touch_sample_t *press,
    const touch_sample_t *release
) {
    const touch_gesture_t base =
        touch_input_classify_release(config, press, release);
    if (tracker == NULL) {
        return base;
    }
    if (base == TOUCH_GESTURE_TAP) {
        const touch_input_config_t bounded = touch_input_clamp(config);
        if (tracker->last_tap_valid &&
            release->timestamp_ms >= tracker->last_tap_at_ms &&
            release->timestamp_ms - tracker->last_tap_at_ms <=
                bounded.double_tap_ms &&
            touch_input_gesture_enabled(config, TOUCH_GESTURE_DOUBLE_TAP)) {
            tracker->last_tap_valid = 0u;
            return TOUCH_GESTURE_DOUBLE_TAP;
        }
        tracker->last_tap_at_ms = release->timestamp_ms;
        tracker->last_tap_valid = 1u;
        return TOUCH_GESTURE_TAP;
    }
    tracker->last_tap_valid = 0u;
    return base;
}
