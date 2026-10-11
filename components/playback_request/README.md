# Playback Request

Caches a bounded list of the guardian's on-demand playback requests so the
device can drain them in order and report each request's terminal outcome,
staying consistent with the parent app's playback view
(`/api/v1/playback/requests`).

A request keeps a stable request id, a content package id, a stable status
(`pending`, `acknowledged`, `completed`, `failed`, `cancelled`), a terminal
outcome (`played`, `skipped`, `error`), an optional bounded note, and the
epoch-seconds time it was requested. The module never stores content bytes,
audio, conversation content, credentials, or raw upstream data.

## Contract

- The cache holds at most `PLAYBACK_REQUEST_CAPACITY` (16) requests.
- Re-upserting an existing request replaces it and moves it to the head so the
  most recently changed entry leads.
- `playback_request_list_pending` returns the requests that still need device
  action (pending or acknowledged), oldest first, so the device drains them in
  the order they were queued.
- `playback_request_acknowledge` and `playback_request_complete` reject a
  request that already reached a terminal state with
  `PLAYBACK_REQUEST_ERR_INVALID_STATE`.
- The cache persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid cache so startup never fails.

## Behavior

`playback_request_upsert`, `playback_request_remove`,
`playback_request_acknowledge`, and `playback_request_complete` mutate the
in-memory cache and persist it. `playback_request_list`,
`playback_request_list_pending`, and `playback_request_get` let the playback
path read the queued requests.
