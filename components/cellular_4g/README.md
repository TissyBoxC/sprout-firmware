# cellular_4g

`cellular_4g` owns the bounded cellular policy contract, the validated
persist/restore path, and the pure connectivity and quota decisions the modem
layer consumes. It never touches audio, voice, or content state, so a Wi-Fi-only
firmware profile simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `monthly_quota_mb` 0..512000 (0 = unlimited)
- `low_quota_warn_percent` 5..100
- `keepalive_seconds` 30..3600
- `preference` one of Wi-Fi-first / cellular-first / cellular-only
- `enabled`, `roaming_allowed`, `remote_wake_enabled`, `metered_upload_limit`
  booleans

`cellular_set` clamps before persisting, `cellular_get` returns the applied
policy, and `cellular_apply` refuses a policy that is not version-compatible or
already bounded.

## Decisions

`cellular_should_attach` encodes the Wi-Fi/cellular preference and rejects a
roaming attach when roaming is off. `cellular_quota_exhausted` and
`cellular_quota_warning` drive the metered upload limit, so non-essential
background uploads stop once the quota is reached while voice and emergency
traffic are unaffected. `cellular_usage_add_mb` rolls the counter at the UTC
day boundary.

## Host tests

`cellular_core` and `cellular_runtime` run under `tools/run_host_tests.ps1` and
cover clamping, preference validation, attach decisions, quota/warning
thresholds, metered upload gating and the persistence round trip.
