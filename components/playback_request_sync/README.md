# playback_request_sync

`playback_request_sync` keeps the device's guardian on-demand request cache
aligned with the platform and reports each local lifecycle change back so the
guardian sees that a request was accepted and how it ended.

## Contract

`GET /api/v1/devices/{device_id}/playback/requests` with the device session
token returns the requests that still need device action (pending or
acknowledged) in `data.requests`. Each entry carries `id`, `package_id`,
`note`, `status` and `requested_at`. `requested_at` is a UTC RFC3339 timestamp;
the device validates it strictly and stores it as epoch seconds so it can order
and age requests without trusting its own clock.

One entry that is missing an id, package id, valid status, or a strict
`YYYY-MM-DDTHH:MM:SSZ` (optionally fractional) `requested_at` rejects the whole
snapshot with `ESP_ERR_INVALID_ARG`. Merging only the surviving rows would drop
a local non-terminal request the guardian never actually cancelled.

The response is merged into the local cache through
`playback_request_merge_remote`:

- a remote request the device has not finished is inserted;
- a remote request the device already finished is ignored so a played request
  is not resurrected as pending;
- a local non-terminal request missing from the snapshot was cancelled by the
  guardian and is dropped;
- local terminal history and the outbound queue are retained.

Every local transition queues one bounded outbound action: `acknowledge` when
the device accepts a request and `complete` with the terminal outcome when it
finishes. A completion supersedes a still-queued acknowledgement for the same
request. After the merge the worker drains the queue with
`POST /api/v1/devices/{device_id}/playback/requests/{request_id}/acknowledge`
and
`POST /api/v1/devices/{device_id}/playback/requests/{request_id}/complete`
(the completion body carries `{"outcome": "played|skipped|error"}`). An accepted
action is dropped from the queue; a `404` for a removed request is discarded
rather than retried; a transient failure leaves the rest of the queue for the
next cycle, so an offline device still reports every outcome once it reconnects.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

## Host tests

`playback_request_core`, `playback_request_sync_core` and
`playback_request_runtime` run under `tools/run_host_tests.ps1`. They cover the
cache merge rules, the bounded outbound queue (dedupe, supersede, drain and
persistence across a restart).
