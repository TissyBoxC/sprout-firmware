# local_command_sync

`local_command_sync` fetches the family's local command catalogue and replaces
the local command cache.

## Contract

- A `401`/`403` clears the device session token so the next request
  re-authenticates.
- The local cache is left untouched when the network, clock or payload is
  unusable.
- Commands are bounded by `local_command`; an invalid remote entry rejects the
  snapshot instead of partially applying it.

## Tests

`local_command_sync_runtime` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; `local_command` keeps the built-in
defaults.
