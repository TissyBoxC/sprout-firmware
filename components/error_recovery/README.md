# error_recovery

Owns degraded-mode decisions, module failure/recovery notifications, and
controlled restarts.

The module registers failure and recovery handlers with `module_registry`, so a
module that fails startup and later initializes successfully produces one
bounded recovery transition. A later failure for the same module closes the
previous recovery cycle, so the next success produces a distinct transition.
`error_recovery_mark_recovered()` is also available to module owners that
recover from a runtime failure without reinitializing.

Failures and recoveries share one monotonic transition counter. That lets
`diagnostic_reporter` order a failure and its recovery even when both happen
between two heartbeats, and lets the platform derive health state from sequence
instead of wall-clock time. The recovery queue keeps the newest bounded
transitions in RAM.

Recovery snapshots contain module names and counters only. Never add
credentials, child data, audio, images, or provider payloads to recovery logs.
