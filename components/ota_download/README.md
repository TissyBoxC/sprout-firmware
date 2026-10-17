# ota_download

`ota_download` is the pure transfer state machine for firmware updates: size
tracking, bounded retries, resume offsets, cancellation and final SHA-256
completion.

## Contract

- A transfer is bounded by the manifest-declared size and a maximum attempt
  count.
- Resuming reports the offset already received so a dropped connection does
  not restart the download.
- Completion requires the expected and actual SHA-256 to match; a mismatch
  surfaces `OTA_DOWNLOAD_CORE_ERR_HASH_MISMATCH`.
- Cancellation is terminal for the current transfer and never reports
  completion.

## Tests

`ota_download_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; `ota_manager` then has no
transfer state machine and the OTA profile must be disabled.
