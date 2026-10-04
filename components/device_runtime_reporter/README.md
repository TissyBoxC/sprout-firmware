# device_runtime_reporter

Sends the versioned `device_runtime` heartbeat over HTTPS after `cloud_auth`
has a usable device session. The reporter includes normalized connection,
network-quality, time-sync, and offline-fallback state, and uses the
`heartbeat_id` as the platform idempotency key.

The module also polls operator commands from the platform runtime API and
acknowledges each command exactly once. It calls `network_manager_reconnect_stored`
for reconnect commands, `time_sync` for clock resynchronization, and
`parent_policy_refresh` for `refresh_configuration`. After a successful
heartbeat the reporter opportunistically refreshes the parent policy when its
configured interval has elapsed; a policy refresh failure is recorded by
`parent_policy` and does not change the heartbeat result. No token, SSID,
password, audio, image, or child data is logged.

When `diagnostic_reporter` is present, the heartbeat also carries a bounded
`diagnostics` object containing persisted boot events and the latest
removable-module failure plus pending module recovery events. Each recovery
entry contains `event_id`, `event_type=module_recovered`, `sequence`,
`module_name`, and `firmware_version`. The extension is optional: a platform
that ignores unknown heartbeat fields continues to receive the original
contract, and a firmware image without `diagnostic_reporter` sends the original
payload. Diagnostic records are acknowledged only after a successful
authenticated heartbeat, so a lost response is retried with the same event
identity.

`CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER` controls source inclusion. Delete the
component, its Kconfig entry, and its registration to remove runtime reporting
while leaving registration, provisioning, and binding intact.
