# button_input

Owns debounced physical key input and converts one or more GPIO buttons into
bounded gesture events. GPIO interrupts only notify the button task; all
sampling, debounce timing, and gesture classification run in normal task
context.

## Gestures

The module reports short press, long press, very-long press, and double press.
A press shorter than the short-press threshold is discarded. A long press is
reported at the long threshold and a very-long press is reported at the
very-long threshold. The application owns the factory-reset action and should
require an explicit user-visible confirmation before erasing data.

## Defaults and pins

The primary button defaults to GPIO 0, the BOOT button on the ESP32-S3 N16R8
profile, and is active-low. A product profile may override the pin when the
enclosure uses a different function key. The optional secondary button
defaults to GPIO 1. Both defaults avoid the active I2S pins 4-7 and the
N16R8 flash/PSRAM pins.

## Public API

`button_input_init` configures the configured GPIOs, installs the shared GPIO
ISR service if needed, and starts the button task. `button_input_is_ready`
reports whether the module can deliver gestures. `button_input_set_event_handler`
registers one callback and passes a bounded event with button index, gesture,
held time, and a millisecond timestamp. The callback runs in the button task
and may call non-ISR APIs.

`button_input_get_snapshot` returns per-button short, long, double, and
very-long counters plus the debounced pressed state. `button_input_gesture_name`
and `button_input_error_name` return stable strings. The module descriptor
provides initialize and shutdown callbacks; shutdown removes this module's ISR
handlers, stops the task, frees resources, and leaves the GPIOs in a safe
input state.

## Removal

Disable `CONFIG_FEATURE_BUTTON_INPUT`, remove the descriptor registration from
the composition root, and remove the conditional dependency in the application
CMake file. No other module needs to change.
