#include "alarm_reminder_sync_core.h"

#include <string.h>

// Reuse the strict store validator by staging each candidate through a
// one-entry upsert, so the remote path and the guardian path agree on exactly
// what a usable alarm is.
static bool alarm_reminder_sync_is_valid(const alarm_reminder_t *alarm) {
    if (alarm == NULL) {
        return false;
    }
    alarm_reminder_core_t probe;
    alarm_reminder_core_init(&probe);
    return alarm_reminder_core_upsert(&probe, alarm) == ALARM_REMINDER_OK;
}

static bool alarm_reminder_sync_contains(
    const char seen_ids[][ALARM_REMINDER_ID_SIZE],
    size_t seen_count,
    const char *alarm_id
) {
    for (size_t index = 0; index < seen_count; ++index) {
        if (strcmp(seen_ids[index], alarm_id) == 0) {
            return true;
        }
    }
    return false;
}

alarm_reminder_error_t alarm_reminder_sync_core_replace_schedule(
    alarm_reminder_core_t *core,
    const alarm_reminder_t *schedule,
    size_t schedule_count
) {
    if (core == NULL) {
        return ALARM_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (schedule == NULL && schedule_count > 0) {
        return ALARM_REMINDER_ERR_INVALID_ARGUMENT;
    }
    if (schedule_count == 0) {
        alarm_reminder_core_init(core);
        return ALARM_REMINDER_OK;
    }

    // Track which entries were accepted so the second pass can build the store
    // without a second full-size schedule on the stack.
    char seen_ids[ALARM_REMINDER_CAPACITY][ALARM_REMINDER_ID_SIZE] = {};
    size_t chosen[ALARM_REMINDER_CAPACITY] = {};
    size_t chosen_count = 0;
    for (size_t index = 0; index < schedule_count; ++index) {
        const alarm_reminder_t *alarm = &schedule[index];
        if (!alarm_reminder_sync_is_valid(alarm) ||
            alarm_reminder_sync_contains(
                seen_ids,
                chosen_count,
                alarm->alarm_id
            )) {
            continue;
        }
        if (chosen_count >= ALARM_REMINDER_CAPACITY) {
            break;
        }
        memcpy(
            seen_ids[chosen_count],
            alarm->alarm_id,
            ALARM_REMINDER_ID_SIZE
        );
        chosen[chosen_count] = index;
        chosen_count += 1;
    }
    if (chosen_count == 0) {
        return ALARM_REMINDER_ERR_INVALID_ARGUMENT;
    }

    alarm_reminder_t rebuilt[ALARM_REMINDER_CAPACITY];
    memset(rebuilt, 0, sizeof(rebuilt));
    for (size_t index = 0; index < chosen_count; ++index) {
        const alarm_reminder_t *remote = &schedule[chosen[index]];
        alarm_reminder_t merged = *remote;
        // Preserve the local firing state of a surviving alarm id.
        const size_t existing = alarm_reminder_core_find(
            core,
            remote->alarm_id
        );
        if (existing < core->count) {
            merged.state = core->alarms[existing].state;
            merged.last_fired_date = core->alarms[existing].last_fired_date;
            merged.snooze_deadline_tick =
                core->alarms[existing].snooze_deadline_tick;
        }
        rebuilt[index] = merged;
    }
    memset(core->alarms, 0, sizeof(core->alarms));
    for (size_t index = 0; index < chosen_count; ++index) {
        core->alarms[index] = rebuilt[index];
    }
    core->count = chosen_count;
    return ALARM_REMINDER_OK;
}
