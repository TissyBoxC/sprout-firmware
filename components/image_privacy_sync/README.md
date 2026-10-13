# image_privacy_sync

`image_privacy_sync` fetches the family image-privacy policy and applies it
through `image_privacy`, keeping the device's processing mode, upload scope,
retention window, redaction level and consent gate consistent with the parent
app.

## Contract

`GET /api/v1/devices/{device_id}/image-privacy/policy` with the device session
token returns the policy in `data`:

- `enabled`, `upload_allowed`, `local_processing_only`
- `processing_mode` one of `off`, `metadata`, `local_full`
- `upload_scope` one of `none`, `thumbnails`, `analysis`
- `retention_days`
- `redaction_level` one of `none`, `standard`, `strict`
- `require_child_consent`
- `revision`

The response is parsed into a bounded policy and applied through
`image_privacy_apply_remote`, which maps the catalog names, clamps every field,
pins the processing mode off when the feature is disabled and revokes upload
whenever it is unauthorised or local-only. A response with a name outside its
catalog is rejected.

After a successful apply the worker reports the bounded pipeline status to
`POST /api/v1/devices/{device_id}/image-privacy/status` so the platform and
admin overview see the live processing mode, upload scope, redaction level,
processed/uploaded/blocked counters, last block reason and the applied policy
revision. The report is best effort: the policy is already applied, so a failed
report is logged without delaying the sync cycle.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

## Host tests

`image_privacy_sync_core` runs under `tools/run_host_tests.ps1` and covers the
mode/scope/redaction mapping, clamping, and the disabled/derived-upload rules.
