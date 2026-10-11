# motion_control

`motion_control` owns the bounded motor, servo and action safety contract, the
validated persist/restore path, the command queue and the pure clamping helpers
the driver loop consumes. It never touches audio, voice or content state, so a
firmware profile without a moving body simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `max_speed_percent` 1..100
- `max_runtime_ms` 200..60000
- `stop_grace_ms` 0..60000
- `enabled`, `require_stop_ack`, `allow_locomotion` booleans

`motion_set` clamps before persisting, `motion_get` returns the applied policy
and `motion_apply` refuses a policy that is not version-compatible or already
bounded.

## Safety

`motion_submit` rejects a disabled policy, an out-of-range actuator, an angle or
speed outside the bounds, and locomotion while `allow_locomotion` is off. Every
accepted command is clamped by `motion_clamp_command`. `motion_tick` latches the
controller to `timeout` once the bounded run time elapses, `motion_request_stop`
latches an explicit `stopped`, and `motion_fault` drops the queue so a fault
cannot keep driving. `motion_should_stop_on_disconnect` stops locomotion on a
lost link and honours the stop-acknowledgement policy for other actions.

## Host tests

`motion_core` and `motion_runtime` run under `tools/run_host_tests.ps1` and cover
clamping, the action classification, runtime bounds, the queue, the lifecycle
latches, fault handling and the persistence round trip.
