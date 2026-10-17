# theme_package_sync

`theme_package_sync` fetches the published theme packs and atomically replaces
the local `theme_package` cache.

## Contract

- The request uses the device session token from `device_binding_client`; a
  `401`/`403` clears the token so the next request re-authenticates.
- The cache is replaced only after the whole snapshot is valid. A malformed
  entry, a pack count above `THEME_PACKAGE_CAPACITY` (8) or a pack with more
  than `THEME_PACKAGE_MEMBER_CAPACITY` (16) members rejects the snapshot and
  keeps the previous cache.
- The platform allows up to 50 members per pack; the device refuses snapshots
  it cannot represent instead of silently truncating published content.
- An empty snapshot clears the cache intentionally.

## Tests

`theme_package_sync_runtime` runs under `tools/run_host_tests.ps1` and covers
the full replace, invalid snapshot preservation, capacity boundaries and the
retry interval.

## Remove safely

Delete the component and its Kconfig switch; `theme_package` keeps serving the
last cached snapshot until the cache is erased.
