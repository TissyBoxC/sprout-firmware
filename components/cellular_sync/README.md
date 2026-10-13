# cellular_sync

`cellular_sync` is the device half of the cellular closed loop. It reads the
family cellular policy, applies it to `cellular_4g` and reports the live modem
status back to the platform.

## Contract

- Reads `GET /api/v1/devices/{device_id}/connectivity/policy` and applies the
  schema version, enable flag, roaming, preference, monthly quota, warning
  threshold, metered-upload limit, keepalive, remote wake and the policy
  revision (persisted with the applied policy so the status report proves which
  revision the device actually runs).
- Reports `POST .../connectivity/status` with the eSIM state, signal
  percentage, roaming flag, used megabytes and the applied policy revision.
  Without a modem provider it reports an absent eSIM, zero signal, no roaming
  and zero usage rather than a fabricated reading.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
