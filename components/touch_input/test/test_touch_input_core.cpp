#include "touch_input.h"
#include "touch_input_core.h"

#include <cassert>

int main() {
    const touch_input_config_t defaults = touch_input_default();
    assert(touch_input_is_valid(&defaults));
    assert(defaults.enabled == 1u);
    assert(defaults.long_press_ms == 700u);

    touch_input_config_t out_of_range = defaults;
    out_of_range.sensitivity = 0u;
    out_of_range.rotation_degrees = 45u;
    out_of_range.long_press_ms = 1u;
    out_of_range.double_tap_ms = 5000u;
    out_of_range.swipe_distance_px = 1u;
    const touch_input_config_t clamped = touch_input_clamp(&out_of_range);
    assert(clamped.sensitivity == TOUCH_INPUT_MIN_SENSITIVITY);
    assert(clamped.rotation_degrees == TOUCH_INPUT_ROTATION_0);
    assert(clamped.long_press_ms == TOUCH_INPUT_MIN_LONG_PRESS_MS);
    assert(clamped.double_tap_ms == TOUCH_INPUT_MAX_DOUBLE_TAP_MS);
    assert(clamped.swipe_distance_px == TOUCH_INPUT_MIN_SWIPE_DISTANCE_PX);
    assert(touch_input_is_valid(&clamped));

    assert(!touch_input_is_valid(nullptr));
    touch_input_config_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!touch_input_is_valid(&wrong_version));
    assert(touch_input_clamp(nullptr).sensitivity == defaults.sensitivity);

    touch_sample_t press = {0u, 100u, 100u, 1u};
    touch_sample_t release = {50u, 102u, 100u, 0u};
    assert(touch_input_classify_release(&defaults, &press, &release) ==
           TOUCH_GESTURE_TAP);

    // A hold past the long-press threshold is a long press.
    release.timestamp_ms = 800u;
    assert(touch_input_classify_release(&defaults, &press, &release) ==
           TOUCH_GESTURE_LONG_PRESS);

    // A horizontal drag past the swipe distance is a swipe.
    release.timestamp_ms = 100u;
    release.x = 200u;
    assert(touch_input_classify_release(&defaults, &press, &release) ==
           TOUCH_GESTURE_SWIPE_RIGHT);
    release.x = 20u;
    assert(touch_input_classify_release(&defaults, &press, &release) ==
           TOUCH_GESTURE_SWIPE_LEFT);
    release.x = 100u;
    release.y = 20u;
    assert(touch_input_classify_release(&defaults, &press, &release) ==
           TOUCH_GESTURE_SWIPE_UP);
    release.y = 200u;
    assert(touch_input_classify_release(&defaults, &press, &release) ==
           TOUCH_GESTURE_SWIPE_DOWN);

    // A disabled panel reports no gesture at all.
    touch_input_config_t disabled = defaults;
    disabled.enabled = 0u;
    release.timestamp_ms = 50u;
    release.x = 102u;
    release.y = 100u;
    assert(touch_input_classify_release(&disabled, &press, &release) ==
           TOUCH_GESTURE_NONE);

    // Turning gestures off individually suppresses just those classes.
    touch_input_config_t tap_off = defaults;
    tap_off.tap_enabled = 0u;
    assert(touch_input_classify_release(&tap_off, &press, &release) ==
           TOUCH_GESTURE_NONE);
    assert(!touch_input_gesture_enabled(&tap_off, TOUCH_GESTURE_DOUBLE_TAP));

    touch_input_tracker_t tracker;
    touch_input_tracker_init(&tracker);
    touch_sample_t tap = {1000u, 100u, 100u, 1u};
    touch_sample_t tap_release = {1050u, 101u, 100u, 0u};
    assert(touch_input_tracker_update(
               &tracker, &defaults, &tap, &tap_release
           ) == TOUCH_GESTURE_TAP);
    touch_sample_t second = {1200u, 100u, 100u, 1u};
    touch_sample_t second_release = {1250u, 101u, 100u, 0u};
    assert(touch_input_tracker_update(
               &tracker, &defaults, &second, &second_release
           ) == TOUCH_GESTURE_DOUBLE_TAP);
    // A third tap after the window restarts the sequence.
    touch_sample_t third = {2000u, 100u, 100u, 1u};
    touch_sample_t third_release = {2050u, 101u, 100u, 0u};
    assert(touch_input_tracker_update(
               &tracker, &defaults, &third, &third_release
           ) == TOUCH_GESTURE_TAP);
    return 0;
}
