# form_factor_profile

`form_factor_profile` owns the capability-composed form-factor contract, the
validated persist/restore path and the pure action-composition helper the
runtime consumes. It never touches voice, content or platform business state, so
a firmware profile with a single body simply does not link the component.

## Contract

Schema version `1`. A profile carries a bounded capability mask, a bounded
action mask and a short technical name. The name is always technical (`desktop`,
`moving_body`, `wearable`); it never binds to a character or an appearance.

`form_factor_clamp` masks the capability set and recomposes the action set, and
`form_factor_is_valid` refuses a profile whose actions do not match its
capabilities so a swapped body cannot advertise an action it cannot perform.

## Composition

`form_factor_compose_actions` is the single source of truth:

- display -> expression and video display
- touch -> touch
- locomotion -> walk, dance and gesture
- servo -> tail and gesture
- microphone and speaker together -> voice
- camera and display together -> video call
- wearable -> location

## Host tests

`form_factor_core` and `form_factor_runtime` run under
`tools/run_host_tests.ps1` and cover composition, clamping, validity, capability
and action queries and the persistence round trip.
