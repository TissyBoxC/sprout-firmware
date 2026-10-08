# parent_control_runtime

One policy-evaluation layer that every consumption path on the device calls
before it lets a child hear audio. Wake detection, voice sessions, content
download scraping, content playback, and the playback queue all funnel through
the same decision function so a guardian change cannot be honoured on one path
but ignored on another.

## Decision contract

`parent_control_evaluate` returns a `parent_control_decision_t` whose `reason`
field is one of these stable values. The values are part of the device
diagnostics contract and must not be renumbered.

| Reason | Meaning |
| --- | --- |
| `PARENT_CONTROL_DECISION_ALLOWED` | The activity may proceed. |
| `PARENT_CONTROL_REASON_DISABLED_PERIOD` | The current local time falls inside a guardian disabled period. |
| `PARENT_CONTROL_REASON_CATEGORY_DENIED` | The content category is not on the guardian allow list. |
| `PARENT_CONTROL_REASON_DAILY_LIMIT_REACHED` | The accumulated active seconds for the local day reached the guardian daily limit. |
| `PARENT_CONTROL_REASON_TIME_UNTRUSTED` | The clock is not trusted and a disabled-period decision is required. |
| `PARENT_CONTROL_REASON_POLICY_UNAVAILABLE` | No policy is cached; the device fails closed. |

`reason_code` carries the same value as a NUL-terminated string for logs and the
usage upload. `policy_version` and `policy_available` describe which cached
policy produced the decision.

## Evaluation order and fail-safe behaviour

1. Safety-exempt audio is always allowed, before any other check.
2. A missing policy denies the activity.
3. When the policy defines disabled periods and the local clock is not trusted,
   the decision is `TIME_UNTRUSTED` (deny) rather than "assume allowed".
4. Disabled periods are checked against the device-local minute of day.
5. A content category that is not on the allow list is denied.
6. The daily limit, computed from the local usage ledger, is checked last.

Disabled periods support cross-midnight windows: a period whose start is later
than its end (for example `21:00`-`07:00`) matches the late-evening phase of one
local day and the early-morning phase of the next.

## Recording real attempts vs. continuation checks

`parent_control_evaluate` records one blocked counter per denied *attempt* so the
guardian sees how often the child was told no. Continuation checks use
`parent_control_evaluate_active` or `parent_control_evaluate_norecord`, which
apply the identical decision math without incrementing blocked counters. This
keeps a long voice session or a playing title from inflating the counts while a
policy is still enforced every tick.

## Safety exemption

Safety prompt tones and crisis prompts are queued at `PLAYBACK_PRIORITY_SAFETY`.
That lane, and only that lane, sets `safety_exempt` when it evaluates, so a
safety announcement plays even inside a disabled period or after the daily
limit is reached. Daily-limit and disabled-period blocks never apply to it.

## Usage accounting helpers

The component owns the small glue that finishes a conversation or a content
playback and commits it to `usage_ledger`:

* `parent_control_note_conversation_finished` records one conversation and its
  elapsed seconds.
* `parent_control_note_content_finished` records one content playback and its
  duration under the correct category.

Elapsed seconds come from the monotonic timer, so a wall-clock correction never
shrinks or inflates the total.

## Configuration

`CONFIG_FEATURE_PARENT_CONTROL_RUNTIME` gates the component. It depends on
`FEATURE_PARENT_POLICY`, `FEATURE_USAGE_LEDGER`, and `FEATURE_TIME_SYNC`. The
component is removable: the verification script deletes it before the
components it requires.
