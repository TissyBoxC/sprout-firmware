#include "habit_reminder_core.h"

#include <string.h>

void habit_reminder_core_init(habit_reminder_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

static bool habit_reminder_is_valid(const habit_reminder_t *reminder) {
    if (reminder == NULL) {
        return false;
    }
    if (reminder->reminder_id[0] == '\0' || reminder->label[0] == '\0') {
        return false;
    }
    if (reminder->minute_of_day >= 24 * 60) {
        return false;
    }
    // A weekday mask must name at least one weekday in 1..7.
    return (reminder->weekday_mask & 0x7Fu) != 0;
}

size_t habit_reminder_core_find(
    const habit_reminder_core_t *core,
    const char *reminder_id
) {
    if (core == NULL || reminder_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->count; ++index) {
        if (strcmp(core->reminders[index].reminder_id, reminder_id) == 0) {
            return index;
        }
    }
    return core->count;
}

habit_reminder_error_t habit_reminder_core_upsert(
    habit_reminder_core_t *core,
    const habit_reminder_t *reminder
) {
    if (core == NULL) {
        return HABIT_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (!habit_reminder_is_valid(reminder)) {
        return HABIT_REMINDER_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = habit_reminder_core_find(
        core,
        reminder->reminder_id
    );
    if (existing >= core->count && core->count >= HABIT_REMINDER_CAPACITY) {
        return HABIT_REMINDER_ERR_FULL;
    }
    if (existing < core->count) {
        // Replace in place so the reminder keeps its original position.
        core->reminders[existing] = *reminder;
        return HABIT_REMINDER_OK;
    }
    core->reminders[core->count] = *reminder;
    core->count += 1;
    return HABIT_REMINDER_OK;
}

habit_reminder_error_t habit_reminder_core_remove(
    habit_reminder_core_t *core,
    const char *reminder_id
) {
    if (core == NULL) {
        return HABIT_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (reminder_id == NULL || reminder_id[0] == '\0') {
        return HABIT_REMINDER_ERR_INVALID_ARGUMENT;
    }
    const size_t index = habit_reminder_core_find(core, reminder_id);
    if (index >= core->count) {
        return HABIT_REMINDER_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->count; ++cursor) {
        core->reminders[cursor] = core->reminders[cursor + 1];
    }
    core->count -= 1;
    memset(&core->reminders[core->count], 0, sizeof(habit_reminder_t));
    return HABIT_REMINDER_OK;
}

habit_reminder_error_t habit_reminder_core_remove_child(
    habit_reminder_core_t *core,
    const char *child_id,
    size_t *removed_out
) {
    if (core == NULL) {
        return HABIT_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (child_id == NULL || child_id[0] == '\0') {
        return HABIT_REMINDER_ERR_INVALID_ARGUMENT;
    }
    size_t removed = 0;
    size_t cursor = 0;
    while (cursor < core->count) {
        if (strcmp(core->reminders[cursor].child_id, child_id) == 0) {
            for (size_t shift = cursor; shift + 1 < core->count; ++shift) {
                core->reminders[shift] = core->reminders[shift + 1];
            }
            core->count -= 1;
            removed += 1;
            continue;
        }
        ++cursor;
    }
    if (removed > 0) {
        memset(&core->reminders[core->count], 0, sizeof(habit_reminder_t));
    }
    if (removed_out != NULL) {
        *removed_out = removed;
    }
    return HABIT_REMINDER_OK;
}

size_t habit_reminder_core_due_at(
    const habit_reminder_core_t *core,
    int weekday,
    uint16_t minute_of_day,
    char (*due_ids_out)[HABIT_REMINDER_ID_SIZE],
    size_t capacity
) {
    if (core == NULL || weekday < 1 || weekday > HABIT_REMINDER_WEEKDAY_COUNT) {
        return 0;
    }
    const uint8_t weekday_bit = (uint8_t)(1u << (weekday - 1));
    size_t matched = 0;
    for (size_t index = 0; index < core->count; ++index) {
        const habit_reminder_t *reminder = &core->reminders[index];
        if (!reminder->enabled) {
            continue;
        }
        if (reminder->minute_of_day != minute_of_day) {
            continue;
        }
        if ((reminder->weekday_mask & weekday_bit) == 0) {
            continue;
        }
        if (due_ids_out != NULL && matched < capacity) {
            memcpy(
                due_ids_out[matched],
                reminder->reminder_id,
                HABIT_REMINDER_ID_SIZE
            );
        }
        matched += 1;
    }
    return matched;
}
