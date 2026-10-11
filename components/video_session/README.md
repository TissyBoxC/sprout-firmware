# video_session

`video_session` owns the bounded video session contract, the validated
persist/restore path and the pure privacy helpers the session loop consumes. It
never touches content or voice state, so a firmware profile without a camera or
microphone simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `ring_timeout_seconds` 5..120
- `max_session_seconds` 60..7200
- `quality_floor` unknown..poor
- `enabled`, `camera_allowed`, `microphone_allowed`, `require_child_consent`
  booleans

The default policy keeps the camera and microphone off, so a device never starts
capturing without an explicit guardian decision.

## Privacy

`video_ring` refuses a disabled policy or a body with no usable transport, and
`video_accept` refuses capture while no media permission is set. A call stays in
`ringing` until `video_accept` runs so a child-consent gate can always decline.
`video_revoke_permission` ends an active call immediately with the
permission-revoked reason, so a revoked permission never leaves a silent capture
running. `video_report_quality` records one bounded band, and
`video_end` maps a bounded reason onto the terminal state.

## Host tests

`video_core` and `video_session_runtime` run under `tools/run_host_tests.ps1` and
cover clamping, capture permission, the ring timeout, the consent gate, the
permission-revocation stop and the persistence round trip.
