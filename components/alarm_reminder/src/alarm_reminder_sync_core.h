#pragma once

#include <stddef.h>

#include "alarm_reminder_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Replace the alarm schedule with one remote snapshot.
 *
 * Remote alarms carry only the schedule; the local firing state (state,
 * last_fired_date and snooze_deadline_tick) of any surviving alarm id is
 * preserved so a schedule refresh never re-rings an alarm the child already
 * dismissed, or drops a pending snooze. Duplicate ids and unusable alarms are
 * dropped, and the result is bounded to the local capacity. An empty remote
 * snapshot clears the schedule because the guardian may deliberately remove
 * every alarm. A non-empty snapshot that ends up empty is rejected so a bad
 * response cannot wipe the family schedule.
 */
alarm_reminder_error_t alarm_reminder_sync_core_replace_schedule(
    alarm_reminder_core_t *core,
    const alarm_reminder_t *schedule,
    size_t schedule_count
);

#ifdef __cplusplus
}
#endif
