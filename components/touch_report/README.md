# touch_report

`touch_report` reports the live touch configuration to the platform so the
guardian overview reflects the gestures the surface will actually recognise.

## Contract

- Capability flags and the enabled gesture count come from `touch_input`.
- The request uses the device session token; a `401`/`403` clears it so the
  next request re-authenticates.
- Reporting is best effort and never blocks input handling.

## Tests

`touch_report_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; touch input keeps working
locally.
