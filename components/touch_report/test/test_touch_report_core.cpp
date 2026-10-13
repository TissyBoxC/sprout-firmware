#include "touch_report_core.h"

#include <cassert>

int main() {
    // A fully enabled surface supports touch and haptics and counts all three
    // gesture classes.
    touch_input_config_t enabled = touch_input_default();
    enabled.enabled = 1u;
    enabled.tap_enabled = 1u;
    enabled.long_press_enabled = 1u;
    enabled.swipe_enabled = 1u;
    enabled.haptic_enabled = 1u;
    touch_status_t full = touch_report_build(&enabled);
    assert(full.touch_supported);
    assert(full.haptic_supported);
    assert(full.gesture_count == 3);

    // Disabling the input layer reports no support at all.
    touch_input_config_t disabled = touch_input_default();
    disabled.enabled = 0u;
    touch_status_t off = touch_report_build(&disabled);
    assert(off.touch_supported == false);
    assert(off.haptic_supported == false);
    assert(off.gesture_count == 0);

    // Only the enabled gesture classes are counted, and haptics can be off
    // independently of touch.
    touch_input_config_t limited = touch_input_default();
    limited.enabled = 1u;
    limited.tap_enabled = 1u;
    limited.long_press_enabled = 0u;
    limited.swipe_enabled = 0u;
    limited.haptic_enabled = 0u;
    touch_status_t limited_status = touch_report_build(&limited);
    assert(limited_status.touch_supported);
    assert(limited_status.haptic_supported == false);
    assert(limited_status.gesture_count == 1);

    // A NULL configuration yields a disabled status.
    touch_status_t null_status = touch_report_build(nullptr);
    assert(null_status.touch_supported == false);
    assert(null_status.haptic_supported == false);
    assert(null_status.gesture_count == 0);

    return 0;
}
