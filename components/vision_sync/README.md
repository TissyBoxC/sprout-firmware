# vision_sync

`vision_sync` is the device half of the camera/vision closed loop. It polls the
platform for pending guardian tasks, marks each task dispatched, runs the
on-device inference provider, and reports one bounded result or failure.

## Contract

- Polls `GET /api/v1/devices/{device_id}/vision/tasks/pending`, then
  `POST .../vision/tasks/{task_id}/acknowledge` before inference so a crash
  mid-task cannot be redelivered forever.
- Reports success through `POST .../vision/results` and failures through
  `POST .../vision/tasks/{task_id}/status` with a stable failure code.
- Uploads only symbolic labels, integer confidence, frame metadata and a short
  summary. Image bytes never cross this component.
- The platform task types map onto `camera_vision`; the firmware-only `capture`
  type is not accepted from the platform.

## Inference provider

An image with a real recognizer registers `vision_sync_register_provider`.
Without a provider the worker still drains the queue but reports every task as
`unsupported`, so the guardian sees an honest failure instead of a fabricated
recognition result.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
