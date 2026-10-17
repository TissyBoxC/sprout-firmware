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
- `revision`

The response is parsed into a bounded policy and applied through
`bluetooth_audio_set`, which clamps out-of-range values and clears the derived
speaker/pairing flags when the feature is disabled. A response with a codec
outside the catalog is rejected so the applied policy never silently changes.

After a successful apply the worker reports the bounded adapter status to
`POST /api/v1/devices/{device_id}/bluetooth-audio/status` so the platform and
admin overview see the live adapter state, link counts, active codec, volume and
the applied policy revision. The policy is already applied when the report
fails; the error is returned to the worker and retried without losing the
successful local update.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

## Host tests

`bluetooth_audio_sync_core` runs under `tools/run_host_tests.ps1` and covers the
codec mapping, clamping, and the disabled-policy derived-flag rules.

`bluetooth_audio_sync_runtime` exercises the real HTTP and worker paths with
host stubs. It covers policy fetch/apply, status reporting, `401`/`403` token
clearing for both requests, malformed payloads, transport and HTTP failures,
report error propagation, worker offline retries, and idempotent
initialization.
