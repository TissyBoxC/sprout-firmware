# camera_capture

Removable camera capture pipeline for camera-equipped firmware images.

## Contract

- The component is compiled only when `CONFIG_CAPABILITY_CAMERA` is set, so a
  device without a camera advertises no `camera` capability and exposes no
  capture entry point.
- Every capture start re-evaluates guardian consent for
  `PRIVACY_GUARD_DATA_IMAGE_UPLOAD`. Without consent the start fails with
  `CAMERA_CAPTURE_ERR_PRIVACY_CONSENT_REQUIRED` and no frame is produced.
- Only bounded metadata (width, height, byte size, sequence, dropped frames)
  is tracked. Frames are validated against shared bounds and rejected frames
  are counted, never stored.
- Image upload consent defaults to deny through `privacy_guard`, matching the
  platform and guardian app privacy defaults.

## Host tests

`tools/run_host_tests.ps1` covers the pure lifecycle core plus the runtime
consent gate through the `privacy_guard` host stubs.
