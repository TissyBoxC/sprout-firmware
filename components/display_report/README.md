# display_report

`display_report` reports the live screen surface to the platform so the
guardian display view matches the panel actually present.

## Contract

- Brightness comes from the applied eye-care policy, the panel size from build
  configuration and touch support from the compiled capability set.
- The request uses the device session token from `device_binding_client`; a
  `401`/`403` clears it so the next request re-authenticates.
- Reporting is best effort and never blocks rendering.

## Tests

`display_report_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch, then remove its registration in
`src/main.c`.
