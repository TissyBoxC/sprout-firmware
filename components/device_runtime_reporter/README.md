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

`CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER` controls source inclusion. Delete the
component, its Kconfig entry, and its registration to remove runtime reporting
while leaving registration, provisioning, and binding intact.
