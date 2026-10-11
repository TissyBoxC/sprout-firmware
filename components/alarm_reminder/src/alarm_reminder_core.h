#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "alarm_reminder.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure alarm reminder store. */
typedef struct {
    alarm_reminder_t alarms[ALARM_REMINDER_CAPACITY];
    size_t count;
} alarm_reminder_core_t;

/** @brief Reset the store to empty. */
void alarm_reminder_core_init(alarm_reminder_core_t *core);

/** @brief Insert or replace one alarm, keeping first-seen order. */
alarm_reminder_error_t alarm_reminder_core_upsert(
    alarm_reminder_core_t *core,
    const alarm_reminder_t *alarm
);

/** @brief Remove one alarm; a missing id is success. */
alarm_reminder_error_t alarm_reminder_core_remove(
    alarm_reminder_core_t *core,
    const char *alarm_id
);

/** @brief Remove every alarm for one child, reporting the removed count. */
alarm_reminder_error_t alarm_reminder_core_remove_child(
    alarm_reminder_core_t *core,
    const char *child_id,
    size_t *removed_out
);

/** @brief Find one alarm index, or count when absent. */
size_t alarm_reminder_core_find(
    const alarm_reminder_core_t *core,
    const char *alarm_id
);

/** @brief Count and optionally collect the alarms due at one time. */
size_t alarm_reminder_core_due_at(
    const alarm_reminder_core_t *core,
    int weekday,
    uint16_t minute_of_day,
    int32_t local_date,
    char (*due_ids_out)[ALARM_REMINDER_ID_SIZE],
    size_t capacity
);

/** @brief Count and optionally collect the snoozes that expire at now_tick. */
size_t alarm_reminder_core_due_snoozes(
    const alarm_reminder_core_t *core,
    int64_t now_tick,
    char (*due_ids_out)[ALARM_REMINDER_ID_SIZE],
    size_t capacity
);

/** @brief Set one alarm's local firing state, clearing any pending snooze. */
alarm_reminder_error_t alarm_reminder_core_set_state(
    alarm_reminder_core_t *core,
    const char *alarm_id,
    alarm_reminder_state_t state
);

/** @brief Set one alarm's pending snooze deadline. */
alarm_reminder_error_t alarm_reminder_core_set_snooze(
    alarm_reminder_core_t *core,
    const char *alarm_id,
    int64_t deadline_tick
);

/** @brief Convert a YYYYMMDD local date and minute of day to an absolute tick. */
int64_t alarm_reminder_core_tick(int32_t local_date, uint16_t minute_of_day);

#ifdef __cplusplus
}
#endif
