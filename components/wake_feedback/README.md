# wake_feedback

Turns one accepted local wake event into immediate device feedback. The module
owns the single wake handler registered with `voice_wake`, plays the built-in
`wake_accepted` cue, selects the `listening` indicator state, and records a
bounded `wake_detected` interaction event for the next authenticated heartbeat.

## Boundaries

`voice_wake` remains the only module that arms detection and enables the
microphone. `wake_feedback` never starts capture, stops capture, or reads raw
audio. Its callback runs on the wake task, so it performs only bounded local
work and never blocks on the network.

The raw wake word name is never sent to the platform. The event carries the
stable ASCII code produced by `voice_wake`, such as
`wake_1_confidence_0700`. `duration_ms` is zero because the detection is
instantaneous; confidence is encoded in the detail code rather than being
misrepresented as a duration. The event is never logged as raw audio or
associated with a child identity.

Each optional integration is guarded by its Kconfig symbol. A build without
`prompt_tone`, `led_indicator`, or `diagnostic_reporter` still accepts wake
events and simply skips the missing feedback channel.

## Public API

`wake_feedback_init` registers the handler after `voice_wake` is ready.
`wake_feedback_is_ready` reports the registration state.
`wake_feedback_get_snapshot` returns bounded counters and the last accepted
wake timestamp. `wake_feedback_module_descriptor` supplies the registry
lifecycle callbacks.

## Removal

Disable `CONFIG_FEATURE_WAKE_FEEDBACK`, remove the descriptor registration
from `src/main.c`, and remove the conditional dependency from
`src/CMakeLists.txt`. `voice_wake` continues to detect wake words without
local cue, indicator, or interaction telemetry.
