# offline_policy

`offline_policy` applies the platform-wide offline download policy on the
device. It owns the bounded policy contract and the validated persist/restore
path the content downloader and offline content index consult before caching a
package.

The policy is a singleton: an operator publishes one version for the whole
fleet, and every bound device reads the same cache budget. The module stores
metadata only; it never touches audio, images, credentials, or child data.

## Contract

Schema version `1`, with every field clamped on write:

- `offline_enabled` boolean
- `max_cached_packages` 1..200
- `max_cache_bytes` 16 MiB..8 GiB
- `cache_ttl_hours` 1..8760
- `download_over_wifi_only` boolean
- `revision` monotonic publish cursor

`offline_policy_set` clamps before persisting, and ignores a policy whose
revision is older than the applied one so an out of order delivery cannot roll
the cache budget back. `offline_policy_get` returns the applied policy.

## Persistence

The policy persists as one versioned binary blob in `config_store`; a missing,
corrupt, or schema-incompatible blob starts from the conservative default so
startup never fails.

## Host tests

`offline_policy_core` and `offline_policy_runtime` run under
`tools/run_host_tests.ps1` and cover clamping, revision gating, the default
fallback, and the persistence round trip.
