# diagnostic_reporter

Owns the bounded, non-sensitive diagnostic state sent with the periodic
`device_runtime` heartbeat. It records one boot event per startup and keeps
the latest removable-module failure plus bounded module recovery events from
`error_recovery`.

The state uses one NVS key, `diag_state`, which is shorter than the ESP-IDF
15-character key limit. Boot events are retained in a bounded queue so a
device that cannot reach the platform does not silently discard the startup
history. The runtime reporter sends pending records with stable event IDs;
only a successful authenticated heartbeat acknowledges them. A lost response
therefore causes a retry with the same event identity instead of a duplicate
business event.

Only bounded module names, symbolic error names, counters, reset reasons, and
firmware versions are stored or transmitted. Tokens, SSIDs, passwords, child
data, audio, images, and provider payloads are never included. The component
does not contact the network directly and cannot bypass TLS or device
authentication.

The module converts the shared `error_recovery` transition counter into
diagnostic sequences in true occurrence order. A failure followed by a
recovery inside one heartbeat interval therefore arrives with increasing
sequences, and the platform can classify the device as recovered rather than
faulted from wall-clock timestamps alone. Each source transition is recorded
once; repeated snapshots of the same failure or recovery reuse the same event
identity until a successful heartbeat acknowledges it.

Recovery events use stable `recovery_<sequence>` IDs and are retained until a
successful authenticated heartbeat acknowledges their sequence. The persisted
state version is bumped whenever the on-flash layout changes, so an older blob
is discarded instead of being interpreted with a newer structure.

`CONFIG_FEATURE_DIAGNOSTIC_REPORTER` controls source inclusion. Removing this
component disables diagnostics without changing the base heartbeat contract.
The `diagnostic_reporter` entry in `tools/verify_removable_modules.ps1` verifies
that another removable module can also be removed without leaving a dangling
reference.
