# offline_policy_sync

`offline_policy_sync` fetches the platform-wide offline download policy and
applies it through `offline_policy`, then reports the applied revision back to
the platform so the console can see which devices are current.

## Contract

- `GET /api/v1/devices/{device_id}/content/offline-policy` with the device
  session token returns the fleet policy in `data`.
- `POST /api/v1/devices/{device_id}/content/offline-policy/acknowledgement`
  with `{"revision": <applied>}` records the revision this device applies.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

The acknowledgement is best effort: the policy is already applied, so a failed
report is logged without failing the sync.
