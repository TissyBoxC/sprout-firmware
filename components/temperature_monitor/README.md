# temperature_monitor

Reads the on-die temperature sensor and derives one normalized thermal band
with a bounded reading, both shared with the `device_runtime` heartbeat.
Operators alert on `hot`/`critical` and the device can use the same band for
its own protective throttling.

The classifier lives in `src/temperature_monitor_core.c`, which has no ESP-IDF
dependency and is covered by a host test. It clamps to the contract bounds
shared with the platform (tenths of a degree Celsius) and returns `unknown`
for an out-of-range or missing reading.

`CONFIG_FEATURE_TEMPERATURE_MONITOR` controls source inclusion. On a board
whose toolchain has no `driver/temperature_sensor.h`, the module compiles the
driver glue out and keeps reporting `unknown`; removing the component leaves
the heartbeat contract valid.
