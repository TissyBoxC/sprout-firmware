#include "touch_input_sync_core.h"

#include <cassert>

int main() {
    // A well-formed platform configuration maps onto the firmware contract.
    const touch_input_config_t config = touch_input_sync_core_build(
        true,
        false,
        true,
        true,
        false,
        70,
        90,
        500,
        250,
        40,
        3
    );
    assert(config.schema_version == TOUCH_INPUT_SCHEMA_VERSION);
    assert(config.enabled == 1u);
    assert(config.tap_enabled == 0u);
    assert(config.swipe_enabled == 1u);
    assert(config.long_press_enabled == 1u);
    assert(config.haptic_enabled == 0u);
    assert(config.sensitivity == 70u);
    assert(config.rotation_degrees == TOUCH_INPUT_ROTATION_90);
    assert(config.long_press_ms == 500u);
    assert(config.double_tap_ms == 250u);
    assert(config.swipe_distance_px == 40u);
    assert(config.revision == 3u);
    assert(touch_input_is_valid(&config));

    // Out-of-range values are clamped instead of installed verbatim, an
    // unsupported rotation falls back to zero, and a negative revision is
    // treated as zero.
    const touch_input_config_t clamped = touch_input_sync_core_build(
        true,
        true,
        true,
        true,
        true,
        999,
        45,
        1,
        99999,
        1,
        -4
    );
    assert(clamped.sensitivity == TOUCH_INPUT_MAX_SENSITIVITY);
    assert(clamped.rotation_degrees == TOUCH_INPUT_ROTATION_0);
    assert(clamped.long_press_ms == TOUCH_INPUT_MIN_LONG_PRESS_MS);
    assert(clamped.double_tap_ms == TOUCH_INPUT_MAX_DOUBLE_TAP_MS);
    assert(clamped.swipe_distance_px == TOUCH_INPUT_MIN_SWIPE_DISTANCE_PX);
    assert(clamped.revision == 0u);
    assert(touch_input_is_valid(&clamped));

    return 0;
}
