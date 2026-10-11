# family_linkage

`family_linkage` owns the bounded family-linkage contract, the validated
persist/restore path and the pure routing and content-sync helpers the runtime
consumes. It never touches voice or content state, so a firmware profile with a
single device simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `group_members` 1..16
- `sync_retry_seconds` 5..3600
- `sync_max_attempts` 1..8
- `conflict_strategy` local_wins..defer
- `enabled` boolean

The default is a family of one with `remote_wins`, so a lone device behaves like
a family of one without a second device.

## Routing and sync

`family_route_allowed` reaches a device unconditionally, a group only when a
second member exists, and the family only when the linkage is enabled, so a
disabled device never broadcasts to the family. `family_resolve_conflict` and
`family_sync_state` implement eventual consistency: a higher version always
wins, and the configured strategy only breaks a same-version tie. A `defer`
tie is reported as `conflict` so it is never silently applied.
`family_record_sync` resets the retry budget on a successful apply and counts a
conflict or failure against `sync_max_attempts`, and
`family_sync_should_retry` is the single inclusive retry predicate.

## Host tests

`family_core` and `family_linkage_runtime` run under
`tools/run_host_tests.ps1` and cover clamping, the routing scope, conflict
resolution, the sync state machine, the retry budget and the persistence round
trip.
