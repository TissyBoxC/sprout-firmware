# wearable_sync

`wearable_sync` is the device half of the wearable location loop. It reads the
family location policy, applies it to `wearable_location`, and reports the
device's position and a latched SOS back to the platform.

## Contract

- Reads `GET /api/v1/devices/{device_id}/wearable/policy` and applies the
  schema version, enabled flag, permission, precision, upload interval,
  retention and geofence radius.
- Reports `POST .../wearable/location` with a bounded position and the applied
  precision band. The platform evaluates the family geofence itself, so the
  device reports `unknown` rather than guessing a boundary state.
- Reports `POST .../wearable/sos` exactly once for a latched trigger, so a
  child's emergency is not duplicated on every poll.
- A revoked permission clears the local SOS and stops sampling and upload.

## Position provider

An image with a real locator registers `wearable_sync_register_provider`.
Without a provider the worker still applies the guardian policy but uploads no
position, so the guardian never sees a fabricated coordinate.

## Session handling

A 401/403 clears the shared device session token so the next request can
re-authenticate instead of retrying a revoked credential.
