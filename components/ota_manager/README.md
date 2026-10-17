# ota_manager

`ota_manager` coordinates the platform OTA event sequence and keeps the
reported upgrade state consistent from download through reboot verification.

## Contract

- The bounded event sequence must start with the download event.
- The canonical success sequence is validated before `upgrade_ready` is
  reported.
- Failure and rollback sequences are terminal; no event may follow them.

## Tests

`ota_manager_event_sequence` and `ota_manager_state` run under
`tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; OTA scheduling must be disabled
in the same profile change.
