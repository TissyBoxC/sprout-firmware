# offline_fallback

Tracks network loss and recovery for the device runtime contract. The module
enters a grace period first, then an explicit offline state, and persists a
bounded count of telemetry items that still need to be sent after recovery.
The count is cleared only after an authenticated heartbeat is accepted by the
platform; a network reconnect alone does not discard the backlog. On the first
reconnect after a real loss it exposes the pending count so the provisioning
reporter can emit `network_reconnected` with `pending_<n>`. It never queues
audio, images, credentials, or child data.

`CONFIG_FEATURE_OFFLINE_FALLBACK` controls source inclusion. Delete the
component, its Kconfig entry, and its registration to remove it; the device
then reports only the raw network state.
