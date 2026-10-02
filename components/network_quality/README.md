# network_quality

Measures Wi-Fi RSSI, an approximate round-trip latency to the connected gateway
or configured service, and a bounded packet-loss estimate. Consumers read one
normalized quality band and the raw values for the device runtime contract.

`CONFIG_FEATURE_NETWORK_QUALITY` controls source inclusion. Delete the
component, its Kconfig entry, and its registration to remove it; runtime status
then reports `unknown` quality instead of failing.
