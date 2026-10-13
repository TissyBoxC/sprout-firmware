# playback_history_sync

`playback_history_sync` reports the locally recorded playback outcomes to the
platform so the guardian sees what the child played and can resume an
interrupted item.

## Contract

Every finished play is queued by `playback_history` as one bounded row keyed by
(package id, played_at). The worker drains the queue with
`POST /api/v1/devices/{device_id}/content/playback` carrying `package_id`,
`package_version`, `outcome` (`completed` / `interrupted`) and `played_seconds`.
An accepted row is dropped from the queue; a `404` for a removed package is
discarded rather than retried; a transient failure leaves the rest of the queue
for the next cycle, so an offline device still delivers its recent history once
it reconnects.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

## Host tests

`playback_history_core` runs under `tools/run_host_tests.ps1` and covers the
bounded history ring plus the pending-report queue (dedupe, bound, drain).
