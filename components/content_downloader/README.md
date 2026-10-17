# content_downloader

`content_downloader` transfers content packages into the dedicated `content`
partition and feeds ready packages into the playback queue.

## Contract

- Packages are bounded to `CONTENT_DOWNLOADER_MAX_PACKAGE_BYTES` and stored
  under `/content`.
- A transfer verifies the declared size and SHA-256 before the package is
  marked ready; a mismatch is reported as
  `CONTENT_DOWNLOADER_ERR_HASH_MISMATCH`.
- Resume state survives a reboot, and a canceled transfer never leaves a
  half-written package marked ready.
- Playback submits at most `PLAYBACK_QUEUE_MAX_FRAMES_PER_ITEM` frames per
  item so a large package cannot starve the queue.

## Tests

`content_download_state` and `content_playback_chunker` run under
`tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch. Content already stored in the
partition stays readable until the partition is erased by factory reset.
