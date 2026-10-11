# wearable_location

`wearable_location` owns the bounded wearable location contract, the validated
persist/restore path and the pure geofence and SOS helpers the runtime consumes.
It never touches voice, content or voice state, so a firmware profile without a
wearable sensor simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `upload_interval_seconds` 30..86400
- `retention_days` 1..90
- `geofence_radius_meters` 50..10000
- `precision` coarse..precise
- `enabled` and `permission_granted` booleans

The default policy keeps sampling off and requires an explicit guardian
permission, so a wearable never reports a position without a decision.

## Privacy

`location_can_sample` refuses a disabled policy or a missing permission, so
revoking the permission stops sampling and upload entirely. `location_should_upload`
is the single upload-window predicate, and `geofence_evaluate` keeps a point
exactly on the radius inside so a boundary reading never flaps the geofence.
`wearable_revoke_permission` clears an active SOS so a revoked permission never
keeps reporting a stale emergency, and `wearable_sos_trigger` latches the SOS
until `wearable_sos_cancel` runs so a momentary button bounce cannot clear an
emergency.

## Host tests

`wearable_core` and `wearable_location_runtime` run under
`tools/run_host_tests.ps1` and cover clamping, the sampling permission, the
upload window, the geofence boundary, the SOS latch, the permission-revocation
stop and the persistence round trip.
