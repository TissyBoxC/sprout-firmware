# form_factor_report

`form_factor_report` reports the composed form-factor profile and capability
mask so the platform serves the action set matching the hardware present.

## Contract

- The technical name and capability mask come from `form_factor_profile`.
- The request uses the device session token; a `401`/`403` clears it so the
  next request re-authenticates.
- Reporting is best effort and never blocks local interaction.

## Tests

`form_factor_report_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; the profile stays available to
local code.
