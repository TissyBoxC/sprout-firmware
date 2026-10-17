# power_report

`power_report` reports the live battery snapshot to the platform so the
guardian charge and power-mode view matches the bounded state the device is
applying.

## Contract

- Charge state, percentage, voltage and power mode come from `battery_power`.
- The request uses the device session token; a `401`/`403` clears it so the
  next request re-authenticates.
- Reporting is best effort and never blocks power-saving transitions.

## Tests

`power_report_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; battery state stays available
locally.
