# display_policy_sync

`display_policy_sync` is the device half of the eye-care display closed loop. It
reads the family display policy and applies it to `display_policy`, so the
brightness, colour temperature, viewing-distance reminder, daily screen-time cap
and break reminder the guardian set in the parent app actually reach the panel.

## Contract

- Reads `GET /api/v1/devices/{device_id}/display/policy` over the device
  session token and applies the schema version, brightness, colour temperature,
  viewing distance, daily limit, break interval, auto-dim window and the
  auto-brightness / night-light / distance-reminder flags.
- Every field is clamped into the shared bounds and the applied policy revision
  is persisted with the policy, so `display_report` can prove which revision the
  device runs and the platform's `up_to_date` reflects reality.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
