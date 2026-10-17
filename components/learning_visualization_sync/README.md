# learning_visualization_sync

`learning_visualization_sync` fetches the family learning-visualization policy
and applies it through `learning_visualization`, keeping the device's feedback
style, animation level, toggles and session ceiling consistent with the parent
app.

## Contract

`GET /api/v1/devices/{device_id}/learning-visualization/policy` with the device
session token returns the policy in `data`:

- `enabled`
- `feedback_style` one of `neutral`, `encouraging`, `playful`, `celebration`
- `animation_level` one of `off`, `subtle`, `full`
- `touch_feedback_enabled`, `reward_effects_enabled`, `lesson_linkage_enabled`
- `max_session_minutes`
- `revision`

The response is parsed into a bounded policy and applied through
`learning_visualization_apply_remote`, which maps the catalog names, clamps
every field and pins the animation off when the feature is disabled. A response
with a style or animation name outside its catalog is rejected.

After a successful apply the worker reports the bounded surface status to
`POST /api/v1/devices/{device_id}/learning-visualization/status` so the platform
and admin overview see the live visual state, animation level, linked lesson,
feedback counter, session length and the applied policy revision. The policy is
already applied when the report fails; the error is returned to the worker and
retried without losing the successful local update.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

## Host tests

`learning_visualization_sync_core` runs under `tools/run_host_tests.ps1` and
covers the style/animation mapping, clamping, and the disabled-policy rule.
