# touch_input

`touch_input` owns the bounded touch configuration contract and the pure
gesture recognizer the display/input layer consumes. It never touches audio,
voice, or content state, so a firmware profile without a touch panel simply
does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `sensitivity` 10..100
- `rotation_degrees` one of 0/90/180/270
- `long_press_ms` 250..3000
- `double_tap_ms` 120..600
- `swipe_distance_px` 16..480
- `enabled`, `tap_enabled`, `swipe_enabled`, `long_press_enabled`,
  `haptic_enabled` booleans

`touch_input_set` clamps before persisting, `touch_input_get` returns the
applied configuration, and `touch_input_apply` refuses a configuration that is
not version-compatible or already bounded.

## Gesture recognition

`touch_input_classify_release` turns one press/release sample pair into a
gesture. Movement beyond `swipe_distance_px` on the dominant axis wins,
otherwise a hold beyond `long_press_ms` is a long press, otherwise it is a tap.
`touch_input_tracker_update` remembers the previous tap so a second tap inside
`double_tap_ms` becomes a double tap. Every gesture is suppressed when its
toggle is off or the panel is disabled, so a disabled panel leaves button,
voice and LED interaction untouched.

## Host tests

`touch_input_core` and `touch_input_runtime` run under
`tools/run_host_tests.ps1` and cover clamping, rotation validation, gesture
classification, double-tap tracking and the persistence round trip.
