# Growth Record

Caches the guardian's recent growth records so the device can celebrate a
milestone or encouragement offline and stay consistent with the parent app's
companionship timeline (`/api/v1/companionship/growth-records`).

A record keeps a stable record id, a soft child id, a stable category
(`milestone`, `encouragement`, `habit`, `emotion`), a bounded title, an
optional bounded note, and the epoch-seconds time it happened. The module never
stores content bytes, conversation content, credentials, or raw upstream data.

## Contract

- The cache holds at most `GROWTH_RECORD_CAPACITY` (24) records.
- Re-upserting an existing record replaces it and moves it to the head so the
  most recently changed entry leads.
- `growth_record_remove_child` drops every record for one child, which the
  device calls when a child profile is deleted.
- The cache persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid cache so startup never fails.

## Behavior

`growth_record_upsert`, `growth_record_remove` and `growth_record_remove_child`
mutate the in-memory cache and persist it. `growth_record_list` and
`growth_record_get` let the playback and celebration paths read the timeline.
