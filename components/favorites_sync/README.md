# favorites_sync

`favorites_sync` fetches the guardian's favourites and atomically replaces the
local favourites cache.

## Contract

- The request uses the device session token from `device_binding_client`.
- A `401`/`403` clears the token so the next request re-authenticates.
- The local cache is left untouched when the network, clock or payload is
  unusable; an empty snapshot clears the cache intentionally.

## Tests

`favorites_sync_runtime` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; local favourites remain readable
through the `favorites` component.
