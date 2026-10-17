# ota_validate

`ota_validate` parses the signed OTA manifest into a fixed-size structure and
rejects every malformed, oversized or semantically unsafe field before a
download starts.

## Contract

- The manifest schema version, channel, hardware revision, artifact URL,
  SHA-256, size, signature and publication time are all validated.
- Unknown, duplicate and missing fields are rejected explicitly.
- Version ordering, minimum source version and rollback policy are enforced.
- The structure is fixed-size so a hostile response cannot cause unbounded
  allocation.

## Tests

`ota_manifest` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; OTA must be disabled because no
manifest can be trusted.
