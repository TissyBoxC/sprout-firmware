# power_policy_sync

`power_policy_sync` is the device half of the battery/power closed loop. It
reads the family power policy and applies it to `battery_power`, so the
low-battery warning threshold, low-power entry threshold, deep-sleep idle
window and charge priority the guardian set in the parent app actually reach
the device.

## Contract

- Reads `GET /api/v1/devices/{device_id}/power/policy` over the device session
  token and applies the schema version, the enabled, low-power, deep-sleep and
  charge-priority flags, the low-battery warning and low-power entry
  percentages and the deep-sleep idle window.
- Every field is clamped into the shared bounds and the applied policy revision
  is persisted with the policy, so `power_report` can prove which revision the
  device runs.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
