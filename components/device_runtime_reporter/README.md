# device_runtime_reporter

Sends the versioned `device_runtime` heartbeat over HTTPS after `cloud_auth`
has a usable device session. The reporter includes normalized connection,
network-quality, time-sync, and offline-fallback state, and uses the
`heartbeat_id` as the platform idempotency key.

The module also polls operator commands and treats both `pending` and
`delivered` as executable, because the platform lists either state until the
device acknowledges it. It acknowledges each command exactly once and calls
`network_manager_reconnect_stored` for reconnect commands, `time_sync` for
clock resynchronization, and `parent_policy_refresh` for
`refresh_configuration`. After a successful heartbeat the reporter
opportunistically refreshes the parent policy when its configured interval has
elapsed; a policy refresh failure is recorded by `parent_policy` and does not
change the heartbeat result. No token, SSID, password, audio, image, or child
data is logged.

For `factory_reset`, the reporter first calls `factory_reset_request` with the
guardian reason and only then calls `factory_reset_confirm`. A failed request
or confirmation does not restart the device. The command ACK is sent before
the delay and restart; if the ACK fails, the device stays running so the
platform can retry the command safely. After a confirmed erase, an RTC_NOINIT
guard records the command ID. If the ACK response is lost and the same command
is polled again during that software-reset cycle, the reporter retries only
the ACK and never erases twice. An ACK that the platform reports as already
handled (`409 command_already_handled`) is treated as idempotent success.
Only a successful or already-handled ACK allows the restart. A full power loss
clears the guard together with the device session and binding, so the platform
cannot redeliver the same command as a pending session command.

The command response buffer is allocated on the heap instead of the worker
task stack; it prefers PSRAM and falls back to internal RAM. The response
handler marks overflow, truncation, missing termination, and JSON envelope
errors as failures instead of forwarding partial data. The request and
diagnostic buffers are cleared before release so credentials and bounded
device state are not left in freed memory.

When interaction diagnostics are present, field names and values follow the
platform device-runtime contract exactly: `event_id`, `event_type`, `sequence`,
`detail_code`, `duration_ms`, and `firmware_version`. `detail_code` is stable
ASCII matching `[A-Za-z0-9_.:-]{1,64}`. A `wake_detected` event carries the
real detection duration, which is zero for the instantaneous WakeNet
transition; confidence is encoded in the detail code and is never substituted
for `duration_ms`.

When `diagnostic_reporter` is present, the heartbeat also carries a bounded
`diagnostics` object containing persisted boot events, the latest
removable-module failure, pending module recovery events, and interaction
events. Each recovery entry contains `event_id`,
`event_type=module_recovered`, `sequence`, `module_name`, and
`firmware_version`. The extension is optional: a platform that ignores unknown
heartbeat fields continues to receive the original contract, and a firmware
image without `diagnostic_reporter` sends the original payload. Diagnostic
records are acknowledged only after a successful authenticated heartbeat, so
a lost response is retried with the same event identity.

`CONFIG_FEATURE_DEVICE_RUNTIME_REPORTER` controls source inclusion. Delete the
component, its Kconfig entry, and its registration to remove runtime reporting
while leaving registration, provisioning, and binding intact.
