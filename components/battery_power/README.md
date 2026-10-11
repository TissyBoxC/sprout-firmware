# battery_power

`battery_power` owns the bounded battery policy contract, the validated
persist/restore path, the pure power-mode decision and the small estimation
helper the gauge path consumes. It never touches audio, voice, or content state,
so a mains-powered firmware profile simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `low_battery_warn_percent` 5..50
- `low_power_enter_percent` 5..50
- `sleep_after_seconds` 60..86400
- `enabled`, `low_power_enabled`, `deep_sleep_enabled`, `charge_priority`
  booleans

`battery_set` clamps before persisting, `battery_get` returns the applied
policy, and `battery_apply` refuses a policy that is not version-compatible or
already bounded.

## Decisions

`battery_decide_mode` returns normal while charging or when the reading is
missing, low power once the pack falls to the entry threshold, and deep sleep
when the idle window elapses at that threshold. `battery_low_warning` drives the
guardian alert, and `battery_wake_source_retained` keeps the configured wake
source during sleep so the device can always be woken. `battery_average_add`
smooths the raw gauge percentage.

## Host tests

`battery_core` and `battery_runtime` run under `tools/run_host_tests.ps1` and
cover clamping, charge-state derivation, mode decisions, low-battery warnings,
wake-source retention, averaging and the persistence round trip.
