# assessment_sync

`assessment_sync` mirrors a guardian-driven learning assessment onto the device:
the device shows the current item and reports the answer the guardian graded on
their phone.

## What the device may and may not do

The firmware has **no speech recognition**, so it **cannot grade a child**. This
component therefore never invents, derives, estimates, or defaults a score. A
score reaches `assessment_sync_submit_answer` only because a guardian supplied
it from the parent app, and the component refuses the call when:

- the score is outside `0-100` (an out-of-range value is refused, never clamped,
  because a clamped score would report a performance the guardian never gave);
- no session is tracked, or the tracked session no longer accepts answers;
- the answer does not match the item currently presented (same item id, type and
  target text). A grade for a different item would be rejected by the platform
  with `422`.

Presenting the item to the child is the caller's job. This component exposes the
parsed item through `assessment_sync_get_status` and logs its identifier; it
does not render it and does not drive audio. Until the parent-app flow that
supplies the score ships, `FEATURE_ASSESSMENT_SYNC` stays disabled.

## Contract

The worker reads the session with the device session token from
`device_binding_client`:

```
GET /api/v1/devices/{device_id}/assessment-sessions/{session_id}
```

Both device routes are protected by `requireDeviceSession`. The response is

```
{ "data": { "session_id", "child_id", "lesson_id", "state", "difficulty",
            "item_index", "correct_count", "answered_count", "streak",
            "mastery", "progress", "max_items", "started_at", "expires_at",
            "finish_reason", "accepts_answers",
            "next_item": { "position", "item_id", "item_type", "text",
                           "difficulty" } } }
```

`next_item` is absent once the run is terminal, so the device presents nothing
rather than inventing a question. A payload with an unknown `state`, a missing
session id, a difficulty outside `1-5`, or an item without text or with a type
other than `word`/`sentence` is rejected as a whole and the previous snapshot is
kept, so a bad response never wipes the item the child is looking at.

A queued answer is reported with

```
POST /api/v1/devices/{device_id}/assessment-sessions/{session_id}/answers
{ "item_id", "item_type", "target_text", "score" }
```

The body deliberately carries **no `position`**. The platform derives the next
slot from its stored answer count, and the device handler rejects unknown fields
(`DisallowUnknownFields`), so sending one would fail with `400`.

Answer outcomes:

| Status | Meaning | Handling |
| --- | --- | --- |
| `201` | recorded | drop from the queue, apply the returned progress |
| `401`/`403` | device session revoked | clear the token, keep the answer |
| `404`/`409`/`422` | session over, belongs to another device, or the answer can never be accepted | drop the answer, keep draining |
| other | transient | keep the answer queued, back off |

The queue is bounded by `ASSESSMENT_SYNC_PENDING_CAPACITY` and rejects a
duplicate item id, so one answer can never occupy two positions. A new session id
discards the previous run's queue, because those answers can never address the
new run's positions.

## Scheduling

The worker sleeps `CONFIG_ASSESSMENT_SYNC_POLL_INTERVAL_SECONDS` after a good
cycle. Consecutive failures double `CONFIG_ASSESSMENT_SYNC_RETRY_INTERVAL_SECONDS`
up to `CONFIG_ASSESSMENT_SYNC_MAX_RETRY_INTERVAL_SECONDS`. It never blocks the
main loop: every request runs on the `assessment` task, and an offline, clock-less,
or unregistered device opens no connection.

## Privacy

`child_id` is read from the response and deliberately **not stored**: the device
needs only the session id and the item. The status snapshot therefore cannot leak
a child identifier, and no code path can log one. The log lines carry the
session id, state, answered count and item id only — never the item text or the
score, which are the child's work.

## Configuration

`CONFIG_FEATURE_ASSESSMENT_SYNC` defaults to `n` and depends on the binding
client, network manager and time sync modules. The poll interval, retry base, retry
cap and task stack size are configurable under the same feature.

## Tests

`assessment_sync_core` and `assessment_sync_runtime` run under
`tools/run_host_tests.ps1`. The core test covers item/state/score validation, the
duplicate and bound guards, queue ordering, item matching and the backoff curve.
The runtime test drives the real HTTP path through stubbed dependencies and
covers the happy pull, the `position`-free upload body, `401` token clear,
`404`/`409`/`422` drop, transient failure keeping the queue, a new session
discarding the old queue, a finished session clearing the item, worker backoff and
clean shutdown.

## Remove safely

Delete the component directory, its `src/main.c` include and
`module_registry_add` block, and the `CONFIG_FEATURE_ASSESSMENT_SYNC` switch. No
other component calls into it; with the feature off, no assessment request is
made and `main.c` registers nothing.
