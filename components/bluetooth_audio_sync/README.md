# bluetooth_audio_sync

`bluetooth_audio_sync` fetches the family Bluetooth audio policy and applies it
through `bluetooth_audio`, keeping the device's speaker, pairing, codec and
volume ceiling consistent with the parent app.

## Contract

`GET /api/v1/devices/{device_id}/bluetooth-audio/policy` with the device session
token returns the policy in `data`:

- `enabled`, `speaker_mode_allowed`, `pairing_allowed`, `auto_reconnect`
- `preferred_codec` one of `auto`, `sbc`, `aac`, `ldac`
- `max_volume_percent` 0..100
- `display_name`

The response is parsed into a bounded policy and applied through
`bluetooth_audio_set`, which clamps out-of-range values and clears the derived
speaker/pairing flags when the feature is disabled. A response with a codec
outside the catalog is rejected so the applied policy never silently changes.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

## Host tests

`bluetooth_audio_sync_core` runs under `tools/run_host_tests.ps1` and covers the
codec mapping, clamping, and the disabled-policy derived-flag rules.
