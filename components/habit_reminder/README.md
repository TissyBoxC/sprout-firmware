# Habit Reminder

Caches the family's habit reminders so the device can nudge a child offline and
stay consistent with the parent app's companionship reminders
(`/api/v1/companionship/habit-reminders`).

A reminder keeps a stable reminder id, a soft child id, a bounded label, an
optional bounded prompt, its local time of day as minutes past midnight, a
weekday bitmask (ISO numbering, bit `weekday - 1`), and an enable flag.

## Contract

- The cache holds at most `HABIT_REMINDER_CAPACITY` (16) reminders.
- Re-upserting an existing reminder replaces it in place so its position is
  stable across edits.
- `habit_reminder_remove_child` drops every reminder for one child, which the
  device calls when a child profile is deleted.
- The cache persists as one binary blob in `config_store`; a missing or corrupt
  blob starts from an empty, valid cache so startup never fails.

## Behavior

`habit_reminder_upsert`, `habit_reminder_remove` and
`habit_reminder_remove_child` mutate the in-memory cache and persist it.
`habit_reminder_list` returns every reminder, and `habit_reminder_due_at` is
the pure scheduler the reminder loop calls once per local minute to find the
enabled reminders for the current ISO weekday and time.
