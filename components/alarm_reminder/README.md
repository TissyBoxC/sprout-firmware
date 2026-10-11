# Alarm Reminder

Caches the family's alarm schedule so the device rings locally and reports the
firing outcome back to the parent app's alarm reminders
(`/api/v1/devices/{device_id}/alarm-reminders`).

An alarm keeps a stable alarm id, a soft child id, a bounded label, an optional
bounded prompt, its local time of day as minutes past midnight, a weekday
bitmask (ISO numbering, bit `weekday - 1`), a snooze window (1..30 minutes) and
an enable flag. Alongside the schedule the cache tracks the local firing state
(`idle`, `ringing`, `snoozed`, `dismissed`, `missed`), the local date it last
fired and a pending snooze deadline.

## Contract

- The cache holds at most `ALARM_REMINDER_CAPACITY` (16) alarms.
- Re-upserting an existing alarm replaces it in place so its position is stable
  across edits.
- `alarm_reminder_due_at` is the pure once-per-minute scheduler: it matches only
  enabled alarms for the current ISO weekday and time and skips an alarm that
  already fired on the given local date, so an alarm rings at most once a day.
- `alarm_reminder_due_snoozes` reports snoozed alarms whose snooze window has
  elapsed. The deadline is an absolute minute-tick (`alarm_reminder_tick`), so a
  snooze that crosses local midnight still expires correctly.
- `alarm_reminder_remove_child` drops every alarm for one child, which the device
  calls when a child profile is deleted.
- The cache persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid cache so startup never fails.

## Behavior

`alarm_reminder_upsert`, `alarm_reminder_remove` and
`alarm_reminder_remove_child` mutate the in-memory cache and persist it.
`alarm_reminder_mark_ringing`, `alarm_reminder_snooze`,
`alarm_reminder_dismiss` and `alarm_reminder_mark_missed` record the local
firing outcome and persist it. `alarm_reminder_list` returns every alarm and
`alarm_reminder_get` returns one by id.
