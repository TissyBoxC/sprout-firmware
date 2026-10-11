# Playback History

Keeps a bounded, newest-first ring of playback records so the device can offer
"继续播放" and report history that matches the platform's
`/api/v1/devices/{device_id}/content/playback` contract.

## Contract

- The ring holds at most `PLAYBACK_HISTORY_CAPACITY` (32) rows. A new record
  drops the oldest row so the table cannot grow without limit.
- Each row carries `package_id`, `package_version`, the outcome
  (`completed` / `interrupted`), `played_seconds` (bounded to one day), and
  `played_at`.
- The ring persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid ring so startup never fails.
- `playback_history_latest_resumable` returns the newest interrupted record so
  the UI can resume the item the child stopped.

## Behavior

`playback_history_record` validates one event, stores it, and persists the ring.
`playback_history_list` copies up to the caller's capacity and reports the true
count. The module never stores audio bytes, conversation content, credentials,
or raw upstream data.
