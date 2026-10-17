# alarm_reminder_sync

`alarm_reminder_sync` keeps the device's alarm schedule aligned with the
family's guardian-managed reminders and reports each local firing outcome back
to the platform.

## Contract

`GET /api/v1/devices/{device_id}/alarm-reminders` with the device session token
returns the enabled schedule in `data.alarms`. Each entry carries `id`,
`child_id`, `label`, `prompt`, `alarm_time` (`HH:MM`), `weekdays` (ISO 1..7,
empty meaning every day) and `snooze_minutes`.

The response replaces the local cache through
`alarm_reminder_replace_schedule`. The local firing state (`state`,
`last_fired_date`, `snooze_deadline_tick`) of a surviving alarm id is preserved,
so a schedule refresh never re-rings an alarm the child already dismissed or
drops a pending snooze. Unusable or duplicate entries are dropped and the
result is bounded to the local capacity; a non-empty snapshot that ends up
empty is rejected so a malformed response cannot wipe the family schedule.

Every local firing transition (`ringing`, `snoozed`, `dismissed`, `missed`)
queues one bounded outcome keyed by (alarm id, local date). After the schedule
refresh the worker drains the queue with
`POST /api/v1/devices/{device_id}/alarm-reminders/{alarm_id}/events` carrying
`{"alarm_date": "YYYY-MM-DD", "state": "..."}`. An accepted event is dropped
from the queue; a `404` for a removed alarm is discarded rather than retried; a
transient failure leaves the event queued for the next cycle, so the guardian's
firing history survives a reboot or an offline window. The sync result reports
that failed upload instead of returning success; a later cycle retries the same
event while the already-applied schedule refresh remains in effect.

The worker runs once immediately after the device is bound, then on the
configured interval. While offline, unauthenticated, or without a synchronized
clock it retries on a short interval. A `401`/`403` clears the session token so
the next request re-authenticates instead of retrying a revoked credential.

## Host tests

`alarm_reminder_core` and `alarm_reminder_sync_core` run under
`tools/run_host_tests.ps1`. They cover the firing-state machine, the bounded
schedule replacement and the pending-event queue (dedupe, bound, drain and
stale-entry removal).
