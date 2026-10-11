# learning_visualization

`learning_visualization` owns the bounded learning-visualization policy, the
validated persist/restore path and the pure surface helpers the touch learning
loop consumes. It never touches content download or voice state, so a firmware
profile without a touch display simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `feedback_style` neutral..celebration
- `animation_level` off..full
- `max_session_minutes` 1..60
- `enabled`, `touch_feedback_enabled`, `reward_effects_enabled`,
  `lesson_linkage_enabled` booleans

The default policy keeps the feature and the animation off, so a device never
animates a learning surface without an explicit guardian decision. Disabling the
feature pins the animation level back to `off`.

## Behaviour

`learning_visualization_begin_lesson` refuses a disabled feature or a disabled
lesson linkage, and it bounds the linked lesson id to a stable lowercase
identifier. `learning_visualization_effective_animation` resolves a disabled or
paused surface to animation `off`, so a requested full animation never reaches
the driver while nothing is running. `learning_visualization_report_feedback`
and `learning_visualization_show_reward` are both gated by the matching toggle
and increment a bounded session counter.
`learning_session_expired` compares the elapsed session minutes inclusively
against the guardian ceiling so a session cannot overrun the bound.

## Host tests

`learning_core` and `learning_visualization_runtime` run under
`tools/run_host_tests.ps1` and cover clamping, the default-off policy, the lesson
linkage gate, the effective-animation resolution, the pause/resume/stop
transitions, the feedback/reward toggles, the session ceiling and the
persistence round trip.
