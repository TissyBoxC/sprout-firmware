# Theme Package

Caches the platform's published theme packs so the device can offer a thematic
quick-play menu that matches the parent app's
`/api/v1/content/theme-packs` list.

A theme pack is an ordered collection of content package ids. The device does
not store theme artwork or audio here; it stores only the pack identity, title,
ordered members, and the catalog revision so it can tell when a cached theme is
stale.

## Contract

- The cache holds at most `THEME_PACKAGE_CAPACITY` (8) packs and each pack at
  most `THEME_PACKAGE_MEMBER_CAPACITY` (16) members.
- Re-upserting an existing pack replaces it and moves it to the head so the
  most recently changed theme leads.
- The cache persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid cache so startup never fails.

## Behavior

`theme_package_upsert` and `theme_package_remove` mutate the in-memory cache and
persist it. `theme_package_get` and `theme_package_contains_package` let the
playback path resolve a theme to its members. The module never stores content
bytes, conversation content, credentials, or raw upstream data.
