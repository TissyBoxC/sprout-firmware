# camera_vision

On-device vision tasks that consume `camera_capture` frames and report bounded
results to the platform.

## Contract

- Enabled only when `CONFIG_FEATURE_CAMERA_VISION` depends on
  `FEATURE_CAMERA_CAPTURE`, so a device without a camera never advertises or
  links the module.
- Each accepted task re-checks guardian `PRIVACY_GUARD_DATA_IMAGE_UPLOAD`
  consent; a revocation fails the next task with
  `CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED`.
- Result payloads carry only the task id, bounded symbolic labels
  (max 16 labels, 64 characters each), integer confidence clamped to 10000,
  and a short summary. Labels are validated against an allow-list of safe
  characters so the JSON payload cannot be used for injection and never
  includes image bytes.

## Host tests

`tools/run_host_tests.ps1` covers the pure encoder/validator and the runtime
consent gate through the `privacy_guard` host stubs.
