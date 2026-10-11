# Favorites

Keeps a bounded, newest-first index of guardian-bookmarked content package ids
so the device can offer a quick-play menu that matches the parent app's
`/api/v1/content/favorites` list.

## Contract

- Capacity is fixed at `FAVORITES_CAPACITY` (24). Adding past the limit reports
  `FAVORITES_ERR_FULL` so the caller can surface a clear message instead of
  silently dropping a bookmark.
- A repeat add is idempotent: the existing bookmark keeps its position.
- The index persists as one binary blob in `config_store`; a missing or
  corrupt blob starts from an empty, valid index so startup never fails.
- Newest bookmarks lead so the quick-play menu shows the most recent choice
  first.

## Behavior

`favorites_add`, `favorites_remove`, and `favorites_toggle` mutate the in-memory
index and persist it. `favorites_list` copies up to the caller's capacity and
reports the true count so the UI can paginate. The module never stores content
bytes, conversation content, credentials, or raw upstream data.
