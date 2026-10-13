# screen_policy_sync

`screen_policy_sync` is the device half of the screen-surface closed loop. It
reads the family screen policy and applies it to `screen_surface`, so the
default expression, animation, point-to-read dictionary and video profile the
guardian set in the parent app actually reach the device.

## Contract

- Reads `GET /api/v1/devices/{device_id}/screen/policy` over the device session
  token and applies the schema version, default expression, animation,
  point-to-read and video flags, the FPS and bitrate caps and the animation
  frame interval.
- Every field is clamped into the shared bounds, the default expression is
  resolved through the shared name catalog and the applied policy revision is
  persisted with the policy, so `screen_report` can prove which revision the
  device runs.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
