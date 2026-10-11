# display_policy

`display_policy` applies the guardian's eye-care and screen-time policy on the
device. It owns the bounded policy contract, the validated persist/restore
path, and the pure daily usage / break accounting the display driver consumes.

It never touches audio, voice, or content state, so a firmware profile without a
screen simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `brightness_percent` 5..100
- `color_temperature_kelvin` 2700..6500
- `min_view_distance_cm` 20..120
- `daily_limit_minutes` 5..600
- `break_interval_minutes` 5..240
- `dim_after_seconds` 10..600
- `auto_brightness`, `night_light`, `distance_reminder` booleans

`display_policy_set` clamps before persisting, `display_policy_get` returns the
applied policy, and `display_policy_apply` refuses a policy that is not
version-compatible or already bounded.

## Usage accounting

`display_policy_report_activity` starts a continuous-use window on the first
call and accumulates active seconds, resetting at each UTC day boundary.
`display_policy_break_due` and `display_policy_daily_limit_reached` let the
caller schedule the break reminder and enforce the daily cap without keeping a
second timer.

## Host tests

`display_policy_core` and `display_policy_runtime` run under
`tools/run_host_tests.ps1` and cover clamping, version rejection, daily rollover,
and the persistence round trip.
