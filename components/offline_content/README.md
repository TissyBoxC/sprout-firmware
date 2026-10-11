# Offline Content

Keeps a bounded index of the content packages pinned for offline playback so
the device keeps playing stories and learning assets with no network. A package
is `local_ready` only after the content library verified its bytes on disk, so
the device never starts a package it cannot play.

Each entry keeps a bounded package id, title, category, package version, size,
a `local_ready` flag, and the epoch-seconds time it was pinned. The module
stores metadata only; content bytes live in the content partition owned by the
content library.

## Contract

- The index holds at most `OFFLINE_CONTENT_CAPACITY` (32) packages.
- Re-pinning an existing package replaces it and moves it to the head so the
  most recently pinned content leads.
- `offline_content_next_playable` returns the newest pinned package whose bytes
  are verified locally, or reports that nothing is playable.
- `offline_content_mark_ready` only succeeds for a pinned package; an unknown id
  returns `ESP_ERR_NOT_FOUND`.
- The index persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid index so startup never fails.

## Behavior

`offline_content_pin`, `offline_content_mark_ready`, and
`offline_content_unpin` mutate the index and persist it. `offline_content_list`,
`offline_content_get`, and `offline_content_next_playable` let the playback path
read what can be played offline, and `offline_content_get_snapshot` reports the
pinned/playable counts for telemetry.
