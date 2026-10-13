# motion_policy_sync

`motion_policy_sync` is the device half of the motion-control closed loop. It
reads the family motion policy and applies it to `motion_control`, so the
enabled flag, stop-ack requirement, locomotion permission, speed cap, runtime
cap and stop-grace window the guardian set in the parent app actually reach the
device.

## Contract

- Reads `GET /api/v1/devices/{device_id}/motion/policy` over the device session
  token and applies the schema version, the enabled, stop-ack and locomotion
  flags, the maximum speed percent, the maximum runtime and the stop-grace
  window.
- Every field is clamped into the shared bounds and the applied policy revision
  is persisted with the policy, so `motion_report` can prove which revision the
  device runs.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
