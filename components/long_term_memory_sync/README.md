# long_term_memory_sync

`long_term_memory_sync` keeps the device's long-term memory aligned with the
platform so a child's remembered facts survive a device replacement or a
reinstall.

## Contract

The worker pulls `GET /api/v1/devices/{device_id}/memories?since_revision=N`
with the device session token from `device_binding_client`. The response is

```
{ "data": { "memories": [ { "memory_key", "memory_value", "confidence",
                            "revision", "updated_at" } ],
            "revision": N } }
```

and is merged into the `long_term_memory` store instead of replacing it, so a
fact the child just learned locally is never overwritten by an older server
copy. Duplicate keys and entries whose key, value or confidence cannot be
stored are dropped; a snapshot larger than the local capacity, a non-empty
snapshot that reduces to nothing usable, or a revision that moves backwards is
rejected as a whole so a malformed response can never erase memories.

The local store is left untouched when the network, clock, transport, or
payload is unusable. An empty snapshot is authoritative and clears the local
store for the device, because the guardian may deliberately delete every
memory.

Locally produced memories are queued with `long_term_memory_sync_enqueue_key`
after a successful `long_term_memory_remember`. The worker reports them in
batches of `LONG_TERM_MEMORY_SYNC_BATCH_SIZE` through
`POST /api/v1/devices/{device_id}/memories` carrying
`{ "memories": [ { "memory_key", "memory_value", "confidence" } ] }`. A batch
counts as synced only after the platform answers with a matching `accepted`
count; a partial acceptance or a transient failure leaves the keys queued, so
an offline device still uploads them after a reboot.

The revision cursor and the pending keys persist in `config_store` under their
own key, separate from the `long_term_memory` blob. A missing or corrupt record
restarts from a full pull rather than trusting a cursor the device cannot back
up.

The request uses the device session token created by `device_binding_client`;
a `401`/`403` clears the token so the next request re-authenticates instead of
retrying a revoked credential. Without guardian authorization the module opens
no connection at all: consent is the precondition for any cloud transfer.

### Privacy

Memory values never reach the log sink. The module logs only entry counts,
revisions, and error status names. Values leave the device solely through the
authenticated HTTPS request.

### Configuration

`CONFIG_LONG_TERM_MEMORY_SYNC_ENABLED` defaults to `n` (secure by default) and
depends on the long-term memory, binding client, network manager and time sync
modules. `CONFIG_LONG_TERM_MEMORY_SYNC_INTERVAL_SECONDS` and
`CONFIG_LONG_TERM_MEMORY_SYNC_RETRY_INTERVAL_SECONDS` tune the cadence.

## Tests

`long_term_memory_sync_core` and `long_term_memory_sync_runtime` run under
`tools/run_host_tests.ps1`.

The core test covers the device-scoped child id, duplicate and bound handling,
unusable entry rejection, the backwards-revision guard, and the pending queue.
The runtime test drives the real HTTP path through stubbed dependencies and
covers the happy pull, incremental `since_revision`, the empty-snapshot clear,
unusable payload / network / clock failures preserving the local store, the
`401`/`403` token clear, the upload-then-acknowledge flow, partial acceptance,
forgotten entries leaving the queue, worker retry and shutdown, and the cursor
surviving a restart.

## Remove safely

Delete the component directory, its `src/main.c` include and
`module_registry_add` block, its `src/CMakeLists.txt` dependency block, and the
`CONFIG_LONG_TERM_MEMORY_SYNC_ENABLED` switch. Local long-term memory keeps
working in full: `long_term_memory` owns its own blob and never calls into this
component.
