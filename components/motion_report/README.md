# motion_report

`motion_report` reports the live motion controller state to the platform so the
guardian overview reflects the action the body is actually running.

## Contract

- Lifecycle state, active action and fault flag come from `motion_control`.
- The request uses the device session token; a `401`/`403` clears it so the
  next request re-authenticates.
- Reporting is best effort and never delays a stop command.

## Tests

`motion_report_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; motion control keeps running
without the platform mirror.
