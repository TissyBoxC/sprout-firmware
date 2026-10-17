# connectivity_report

`connectivity_report` reports the live network bearer and measured signal to
the platform so the guardian overview reflects the connection the device is
actually using.

## Contract

- `connectivity_report_now` uses the device session token from
  `device_binding_client`.
- A `401`/`403` clears the session token so the next request re-authenticates
  instead of retrying a revoked credential.
- The report is best effort and never blocks playback or the voice path.

## Tests

`connectivity_report_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch, then remove its registration in
`src/main.c`.
