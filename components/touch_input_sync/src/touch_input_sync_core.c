#include "touch_input_sync_core.h"

static uint32_t to_u32(int value) {
    return value < 0 ? 0u : (uint32_t)value;
}

// Clamp an int platform value into a documented range before narrowing, so a
// value wider than the target field cannot wrap into an in-range number.
static uint8_t clamp_int_to_u8(int value, int minimum, int maximum) {
    if (value < minimum) {
        value = minimum;
    }
    if (value > maximum) {
        value = maximum;
    }
    return (uint8_t)value;
}

static uint16_t clamp_int_to_u16(int value, int minimum, int maximum) {
    if (value < minimum) {
        value = minimum;
    }
    if (value > maximum) {
        value = maximum;
    }
    return (uint16_t)value;
}

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
) {
    touch_input_config_t config = touch_input_default();
    config.enabled = enabled ? 1u : 0u;
    config.tap_enabled = tap_enabled ? 1u : 0u;
    config.swipe_enabled = swipe_enabled ? 1u : 0u;
    config.long_press_enabled = long_press_enabled ? 1u : 0u;
    config.haptic_enabled = haptic_enabled ? 1u : 0u;
    config.sensitivity = clamp_int_to_u8(
        sensitivity,
        (int)TOUCH_INPUT_MIN_SENSITIVITY,
        (int)TOUCH_INPUT_MAX_SENSITIVITY
    );
    // An unsupported rotation falls back to the identity rotation inside the
    // shared clamp; a negative or widened value lands there too.
    config.rotation_degrees = rotation_degrees < 0
        ? (uint16_t)TOUCH_INPUT_ROTATION_0
        : (uint16_t)rotation_degrees;
    config.long_press_ms = clamp_int_to_u16(
        long_press_ms,
        (int)TOUCH_INPUT_MIN_LONG_PRESS_MS,
        (int)TOUCH_INPUT_MAX_LONG_PRESS_MS
    );
    config.double_tap_ms = clamp_int_to_u16(
        double_tap_ms,
        (int)TOUCH_INPUT_MIN_DOUBLE_TAP_MS,
        (int)TOUCH_INPUT_MAX_DOUBLE_TAP_MS
    );
    config.swipe_distance_px = clamp_int_to_u16(
        swipe_distance_px,
        (int)TOUCH_INPUT_MIN_SWIPE_DISTANCE_PX,
        (int)TOUCH_INPUT_MAX_SWIPE_DISTANCE_PX
    );
    config.revision = to_u32(revision);
    // The clamp pins every field into the shared bounds and restamps the
    // schema version, so a malformed platform value can never reach the touch
    // controller.
    return touch_input_clamp(&config);
}
