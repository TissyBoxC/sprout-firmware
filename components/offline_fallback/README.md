# offline_fallback

Tracks network loss and recovery for the device runtime contract. The module
enters a grace period first, then an explicit offline state, and records a
bounded count of telemetry items that still need to be sent after recovery. It
never queues audio, images, credentials, or child data.

`CONFIG_FEATURE_OFFLINE_FALLBACK` controls source inclusion. Delete the
component, its Kconfig entry, and its registration to remove it; the device
then reports only the raw network state.
