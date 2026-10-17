# content_library

`content_library` is the bounded local index of content packages and their
download state. It stores metadata only; package bytes live in the content
partition owned by `content_downloader`.

## Contract

- Each entry carries the package id, version, title, category, age tiers,
  SHA-256, size, publication time, local path and lifecycle state.
- Manifest operations are `upsert` and `withdraw`; a withdrawal marks an entry
  unavailable without deleting the audit trail.
- The index persists as a bounded blob and starts empty from a corrupt store.

## Tests

`content_library_index` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch, then drop the package manager and
download references that depend on it.
