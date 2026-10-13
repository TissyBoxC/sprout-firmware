# connectivity_policy_sync

`connectivity_policy_sync` is the Wi-Fi-path half of the connectivity closed
loop. It reads the family connectivity policy and persists the applied revision
so `connectivity_report` can report where the device actually is, instead of a
hardcoded zero that never converges.

## Contract

- Reads `GET /api/v1/devices/{device_id}/connectivity/policy` over the device
  session token, maps the enabled, roaming, remote-wake, metered-upload and
  preference fields and persists the applied revision.
- `connectivity_policy_applied_revision` returns the persisted revision, which
  `connectivity_report` sends as `policy_revision`.

## Scope

This component is only built for a Wi-Fi-only image. A build that links
`cellular_sync` uses that component as the single connectivity policy source
and never builds this one, so the two never race to apply the same policy.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
