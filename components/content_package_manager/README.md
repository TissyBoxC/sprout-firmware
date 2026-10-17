# content_package_manager

`content_package_manager` synchronises the published content manifest into the
local `content_library` index and drives package downloads.

## Contract

- `content_package_manager_start` resumes manifest sync and pending downloads.
- The manager exposes a bounded progress snapshot for the device UI: state,
  last error, catalog revision, package counters and active transfer progress.
- A failed manifest or download leaves the previous index intact and exposes a
  stable reason from `content_package_error_t`.

## Tests

`content_package_state` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; the index and stored packages can
then be erased by factory reset.
