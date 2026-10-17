# video_session_sync

`video_session_sync` fetches the family video-session policy and applies it
through `video_session`, keeping the device's camera and microphone
permissions, child consent gate, quality floor, ring timeout and session
ceiling consistent with the parent app.

## Contract

`GET /api/v1/devices/{device_id}/video-session/policy` with the device session
token returns the policy in `data`:

- `enabled`, `camera_allowed`, `microphone_allowed`, `require_child_consent`
- `quality_floor` one of `unknown`, `good`, `fair`, `poor`
- `ring_timeout_seconds`
- `max_session_seconds`
- `revision`

The response is parsed into a bounded policy and applied through
`video_apply_remote`, which maps the catalog name and clamps every field. A
response with a quality floor outside its catalog is rejected.

After a successful apply the worker reports the bounded session status to
`POST /api/v1/devices/{device_id}/video-session/status` so the platform and
admin overview see the live call state, quality band, end reason and the
applied policy revision. The sync result reports a failed status upload while
the already-applied policy remains active, so a later cycle retries it.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

## Host tests

`video_session_sync_core` runs under `tools/run_host_tests.ps1` and covers the
quality-floor mapping, clamping, and the consent/permission gates.
