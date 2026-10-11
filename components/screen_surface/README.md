# screen_surface

`screen_surface` owns the bounded screen contract, the validated persist/restore
path and the pure helpers the display driver consumes. It never touches audio or
content state, so a firmware profile without a display simply does not link the
component.

## Contract

Schema version `1`, with every field clamped on write:

- `default_expression` neutral..excited
- `video_max_fps` 5..60
- `video_max_bitrate_kbps` 100..8000
- `animation_frame_ms` 40..1000
- `enabled`, `animation_enabled`, `point_read_enabled`, `video_enabled` bool

The default is an enabled happy face with animation and point-to-read on and
video at a low bitrate, so a slow link still plays instead of failing.

## Expressions, animation, point-to-read and video

`screen_surface_show_expression` clamps to the policy default when the surface
is off, so a stale face never lingers. `screen_surface_play_animation` and
`screen_surface_advance_animation` advance a frame clock only while a clip is
playing and the policy allows it. The point-to-read store is bounded to
`SCREEN_POINT_READ_CAPACITY` entries, normalizes each term, replaces a duplicate
and refuses an empty term; lookup fails closed when point-to-read is disabled.
`screen_video_plan` clamps a requested profile to both the platform bounds and
the policy ceiling, so an unsupported stream never reaches the decoder.

## Host tests

`screen_core` and `screen_surface_runtime` run under
`tools/run_host_tests.ps1` and cover clamping, expression and animation
validation, the frame clock, the point-to-read store and lookup, the video
state machine and the persistence round trip.
