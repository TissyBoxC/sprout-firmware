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
state version is bumped whenever the on-flash layout changes. Known v3 and v4
layouts are migrated to v5: boot events, the latest pending failure, recovery
events, and v4 interaction events are retained with their existing sequence
identities. The migration builds a temporary v5 state, validates it, and only
then writes it back. If that write fails, the in-memory state is cleared
instead of continuing with a partially migrated record. Only an unknown or
corrupt layout is discarded.

The v5 validator requires each category to increase internally, rejects
duplicate sequence numbers across categories, and requires `next_sequence` to
exceed the global maximum. It deliberately does not require one global
concatenation order, because ACK filtering can leave categories interleaved
while their original sequence identities remain valid. Failed NVS writes
restore the complete previous state, including the oldest retained record,
capacity-eviction result, and next sequence. ACK compaction removes only
records with `sequence <= through_sequence`; newer records remain pending.

Interaction events are accepted only for the eight platform event types.
`detail_code` must match the platform symbolic identifier pattern, and
`duration_ms` represents real elapsed time only. Instantaneous events such as
wake detection, rejection, indicator changes, and factory-reset transitions
use zero. Wake confidence is encoded in the stable detail code instead of
being misrepresented as a duration.

The validator rejects unterminated fixed-size strings, invalid counts,
over-capacity records, duplicate or regressing sequences, and invalid v4
interaction fields before an old blob can be used. This keeps boot, failure,
recovery, and interaction history intact across an interrupted or incompatible
write.

`test/test_diagnostic_reporter_state.cpp` covers v3/v4 migration, v5
validation, capacity boundaries, duplicate and out-of-order sequences,
unterminated strings, ACK retention, and the wake `duration_ms == 0`
contract.

`CONFIG_FEATURE_DIAGNOSTIC_REPORTER` controls source inclusion. Removing this
component disables diagnostics without changing the base heartbeat contract.
The `diagnostic_reporter` entry in `tools/verify_removable_modules.ps1` verifies
that another removable module can also be removed without leaving a dangling
reference.
