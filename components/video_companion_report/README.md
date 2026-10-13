# video_companion_report

`video_companion_report` reports the device's live call state and media
permissions to the platform's video-companion device status endpoint. It reads
the current snapshot from `video_session`, so the reported state is the same
session the firmware actually runs.

## Why this exists

The platform `video_companion` module exposes the device's latest metadata to
the parent app through
`GET /api/v1/devices/{device_id}/video-companion/capability`. That read composes
the stored `device_video_status` row for the device. Before this component no
firmware producer ever wrote that row, so every device read back as
unsupported. This closes the console-facing video-companion loop.

## Contract

`POST /api/v1/devices/{device_id}/video-companion/status` with the device
session token carries:

- `call_state` one of `idle`, `ringing`, `active`, `ended`, `rejected`,
  `missed`, `failed`
- `quality` one of `unknown`, `good`, `fair`, `poor`
- `camera_allowed`, `microphone_allowed`
- `last_call_id`

The call state and quality band are mapped from the firmware enums to their
stable platform names. A device with no capture permission reports `idle` with
an `unknown` quality, so a revoked permission never reads as a live call.

## Behaviour

The worker runs once after the device is bound, then on the configured
interval. While offline, unauthenticated, or without a synchronized clock it
retries on a short interval. A `401`/`403` clears the session token so the next
request re-authenticates instead of retrying a revoked credential.

## Host tests

`video_companion_report_core` runs under `tools/run_host_tests.ps1` and covers
the state and quality mapping plus the no-permission collapse.
