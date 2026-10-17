# ui_text_sync

`ui_text_sync` pulls the published `device_firmware` UI text package and
applies it locally.

## Contract

- The device holds exactly one active UI text package; the newest eligible
  publication wins.
- A withdrawal reverts to built-in text.
- The request uses the device session token; a `401`/`403` clears it so the
  next request re-authenticates.
- The local package is left untouched when the network, clock or payload is
  unusable.

## Tests

`ui_text_sync_core` runs under `tools/run_host_tests.ps1`.

## Remove safely

Delete the component and its Kconfig switch; `ui_text` keeps serving built-in
strings.
