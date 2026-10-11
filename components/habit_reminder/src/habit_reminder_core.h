#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "habit_reminder.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure habit reminder store. */
typedef struct {
    habit_reminder_t reminders[HABIT_REMINDER_CAPACITY];
    size_t count;
} habit_reminder_core_t;

/** @brief Reset the store to empty. */
void habit_reminder_core_init(habit_reminder_core_t *core);

/** @brief Insert or replace one reminder, keeping first-seen order. */
habit_reminder_error_t habit_reminder_core_upsert(
    habit_reminder_core_t *core,
    const habit_reminder_t *reminder
);

/** @brief Remove one reminder; a missing id is success. */
habit_reminder_error_t habit_reminder_core_remove(
    habit_reminder_core_t *core,
    const char *reminder_id
);

/** @brief Remove every reminder for one child, reporting the removed count. */
habit_reminder_error_t habit_reminder_core_remove_child(
    habit_reminder_core_t *core,
    const char *child_id,
    size_t *removed_out
);

/** @brief Find one reminder index, or count when absent. */
size_t habit_reminder_core_find(
    const habit_reminder_core_t *core,
    const char *reminder_id
);

/** @brief Count and optionally collect the reminders due at one time. */
size_t habit_reminder_core_due_at(
    const habit_reminder_core_t *core,
    int weekday,
    uint16_t minute_of_day,
    char (*due_ids_out)[HABIT_REMINDER_ID_SIZE],
    size_t capacity
);

#ifdef __cplusplus
}
#endif
