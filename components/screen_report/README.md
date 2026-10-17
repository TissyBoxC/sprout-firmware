# screen_report

`screen_report` reports the live screen surface state to the platform so the
guardian overview reflects what the surface is actually rendering.

## Contract

- Capability flags and the active expression, animation and video state come
  from `screen_surface`.
- The request uses the device session token; a `401`/`403` clears it so the
  next request re-authenticates.
- Reporting is best effort and never blocks rendering.

## Tests

`screen_report_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; the screen surface keeps
rendering locally.
