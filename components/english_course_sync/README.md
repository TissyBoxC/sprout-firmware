# english_course_sync

`english_course_sync` fetches the published English lessons and replaces the
local lesson cache, then reports queued practice attempts.

## Contract

- A `401`/`403` clears the device session token so the next request
  re-authenticates instead of retrying a revoked credential.
- The cached lessons and the local practice log are left untouched when the
  network, clock or response payload is unusable.
- Practice attempts stay queued through transient failures and are delivered on
  a later cycle.

## Tests

`english_course_sync_runtime` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; the English lesson module keeps
working from its built-in content.
