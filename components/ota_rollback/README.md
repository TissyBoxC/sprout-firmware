# ota_rollback

`ota_rollback` decides whether a candidate image may boot and when the
bootloader should roll back.

## Contract

- A version downgrade is rejected unless the manifest explicitly allows
  rollback or the current image is already invalid.
- A pending-verify image that exceeds the boot attempt limit is rolled back.
- A healthy image is marked valid and the attempt counter resets.

## Tests

`ota_rollback_policy` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch together with the OTA profile.
