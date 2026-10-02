# device_runtime_reporter

Sends the versioned `device_runtime` heartbeat over HTTPS after `cloud_auth`
has a usable device session. The reporter includes normalized connection,
network-quality, time-sync, and offline-fallback state, and uses the
`heartbeat_id` as the platform idempotency key.

The module also polls operator commands from the platform runtime API and
acknowledges each command exactly once. It calls `network_manager_reconnect_stored`
for reconnect commands and `time_sync` for clock resynchronization. No token,
SSID, password, audio, image, or child data is logged.

`CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER` controls source inclusion. Delete the
component, its Kconfig entry, and its registration to remove runtime reporting
while leaving registration, provisioning, and binding intact.
