# image_privacy

`image_privacy` owns the bounded image-processing contract, the validated
persist/restore path and the pure policy and pipeline helpers the camera loop
consumes. It never touches content or voice state, so a firmware profile
without a camera simply does not link the component.

## Contract

Schema version `1`, with every field clamped on write:

- `processing_mode` off / metadata / local_full
- `upload_scope` none / thumbnails / analysis
- `retention_days` 0..30
- `redaction_level` none / standard / strict
- `enabled`, `upload_allowed`, `local_processing_only`,
  `require_child_consent` booleans

The default policy keeps processing and upload off and keeps processing
local-only, so a device never uploads a frame without an explicit guardian
decision.

## Behaviour

`image_privacy_begin_processing` refuses a frame unless the feature is enabled
and the processing mode is not off. `image_privacy_authorize_upload` refuses an
upload unless upload is authorised, the policy is not local-only and the
reported scope matches the guardian scope. Disabling the feature pins the mode
off and revokes upload in one step. `image_privacy_record_upload` keeps the
uploaded count a subset of the processed count, and every refusal records a
bounded block reason.

## Host tests

`image_core` and `image_privacy_runtime` run under `tools/run_host_tests.ps1`
and cover clamping, the default-off policy, the processing/upload gates, the
local-only revoke, the counter ceiling, the block reasons and the persistence
round trip.
