# led_indicator

Owns the single status LED shown on the device enclosure. The module maps
device lifecycle and conversation states to bounded LEDC PWM patterns and
keeps those patterns local to one FreeRTOS task. It does not control any other
LED, camera, microphone, or display hardware.

## States and patterns

| State | Pattern |
| --- | --- |
| `off` | Output off |
| `booting` | Slow blink |
| `provisioning` | Slow blink |
| `wifi_connecting` | Slow blink |
| `connecting` | Slow blink |
| `idle` | Solid |
| `listening` | Breathing |
| `thinking` | Breathing |
| `speaking` | Solid |
| `muted` | Output off |
| `error` | Fast blink |
| `low_battery` | Short blink with a long pause |
| `factory_reset` | Fast blink |
| `ota_upgrading` | Slow blink |

The `led_indicator_set_enabled` API is the guardian control. Disabling it
forces the output off and stops the pattern task from applying pattern steps.
Re-enabling restores the currently selected state.

## Hardware configuration

The Kconfig defaults are intended for the current product profile:

- `CONFIG_LED_INDICATOR_GPIO=38`
- active-high (`CONFIG_LED_INDICATOR_ACTIVE_LOW=n`)

There is no repository board definition that proves a different polarity, so
the module uses active-high unless the product profile overrides it. The
module validates the GPIO before initializing LEDC. If the pin or peripheral is
unavailable, initialization returns an error and the module stays not ready.

PWM defaults are 1 kHz at 10-bit duty resolution and 50 percent brightness.
The pattern task uses a 3 KiB stack, priority 2, and a 20 ms transition poll
interval. Every wait is a semaphore or FreeRTOS tick delay, so the task does
not busy-spin.

## Removal

Disable `CONFIG_FEATURE_LED_INDICATOR`, remove the module registration from
`src/main.c`, and remove the conditional dependency from `src/CMakeLists.txt`.
When the feature is disabled, CMake registers no source and the component
links no LEDC driver code.

## Public API

`led_indicator_init` validates and configures the LED, then starts the bounded
pattern task. `led_indicator_set_state` selects one of the stable device
states. `led_indicator_get_state` and `led_indicator_get_snapshot` return the
current bounded state. `led_indicator_set_enabled` is the guardian on/off
control. `led_indicator_state_name` and `led_indicator_error_name` return
stable contract strings. `led_indicator_module_descriptor` supplies the
registry lifecycle callbacks.
