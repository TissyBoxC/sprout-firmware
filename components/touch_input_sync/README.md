# touch_input_sync

`touch_input_sync` is the device half of the touch-input closed loop. It reads
the family touch configuration and applies it to `touch_input`, so the enabled
flag, tap/swipe/long-press/haptic toggles, sensitivity, rotation and gesture
windows the guardian set in the parent app actually reach the device.

## Contract

- Reads `GET /api/v1/devices/{device_id}/touch/policy` over the device session
  token and applies the schema version, the enabled, tap, swipe, long-press and
  haptic flags, the sensitivity, rotation, long-press window, double-tap window
  and swipe-distance threshold.
- Every field is clamped into the shared bounds and the applied config revision
  is persisted with the config, so `touch_report` can prove which revision the
  device runs.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
