#include "alarm_reminder_core.h"

#include <string.h>

static bool alarm_reminder_is_leap_year(int32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

// Counts whole days from 1970-01-01 to the given proleptic Gregorian date. This
// is the classic days-from-civil conversion (Howard Hinnant), valid for any
// year and independent of any real-time clock.
static int64_t alarm_reminder_days_from_civil(
    int32_t year,
    int32_t month,
    int32_t day
) {
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t year_of_era = (int64_t)year - era * 400;
    const int64_t day_of_year =
        (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

int64_t alarm_reminder_core_tick(int32_t local_date, uint16_t minute_of_day) {
    if (local_date <= 0 || minute_of_day >= 24 * 60) {
        return 0;
    }
    const int32_t year = local_date / 10000;
    const unsigned month = (unsigned)((local_date / 100) % 100);
    const unsigned day = (unsigned)(local_date % 100);
    if (year < 1970 || month < 1 || month > 12 || day < 1) {
        return 0;
    }
    static const unsigned days_per_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    unsigned max_day = days_per_month[month - 1];
    if (month == 2 && alarm_reminder_is_leap_year(year)) {
        max_day = 29;
    }
    if (day > max_day) {
        return 0;
    }
    return alarm_reminder_days_from_civil(year, month, day) * 1440 +
           (int64_t)minute_of_day;
}

void alarm_reminder_core_init(alarm_reminder_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

static bool alarm_reminder_is_valid(const alarm_reminder_t *alarm) {
    if (alarm == NULL) {
        return false;
    }
    if (alarm->alarm_id[0] == '\0' || alarm->label[0] == '\0') {
        return false;
    }
    if (alarm->minute_of_day >= 24 * 60) {
        return false;
    }
    if (alarm->snooze_minutes < ALARM_REMINDER_MIN_SNOOZE_MINUTES ||
        alarm->snooze_minutes > ALARM_REMINDER_MAX_SNOOZE_MINUTES) {
        return false;
    }
    if (alarm->state > ALARM_REMINDER_STATE_MISSED) {
        return false;
    }
    // A weekday mask must name at least one weekday in 1..7.
    return (alarm->weekday_mask & 0x7Fu) != 0;
}

size_t alarm_reminder_core_find(
    const alarm_reminder_core_t *core,
    const char *alarm_id
) {
    if (core == NULL || alarm_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->count; ++index) {
        if (strcmp(core->alarms[index].alarm_id, alarm_id) == 0) {
            return index;
        }
    }
    return core->count;
}

alarm_reminder_error_t alarm_reminder_core_upsert(
    alarm_reminder_core_t *core,
    const alarm_reminder_t *alarm
) {
    if (core == NULL) {
        return ALARM_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (!alarm_reminder_is_valid(alarm)) {
        return ALARM_REMINDER_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = alarm_reminder_core_find(core, alarm->alarm_id);
    if (existing >= core->count && core->count >= ALARM_REMINDER_CAPACITY) {
        return ALARM_REMINDER_ERR_FULL;
    }
    if (existing < core->count) {
        // Replace in place so the alarm keeps its original position.
        core->alarms[existing] = *alarm;
        return ALARM_REMINDER_OK;
    }
    core->alarms[core->count] = *alarm;
    core->count += 1;
    return ALARM_REMINDER_OK;
}

alarm_reminder_error_t alarm_reminder_core_remove(
    alarm_reminder_core_t *core,
    const char *alarm_id
) {
    if (core == NULL) {
        return ALARM_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (alarm_id == NULL || alarm_id[0] == '\0') {
        return ALARM_REMINDER_ERR_INVALID_ARGUMENT;
    }
    const size_t index = alarm_reminder_core_find(core, alarm_id);
    if (index >= core->count) {
        return ALARM_REMINDER_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->count; ++cursor) {
        core->alarms[cursor] = core->alarms[cursor + 1];
    }
    core->count -= 1;
    memset(&core->alarms[core->count], 0, sizeof(alarm_reminder_t));
    return ALARM_REMINDER_OK;
}

alarm_reminder_error_t alarm_reminder_core_remove_child(
    alarm_reminder_core_t *core,
    const char *child_id,
    size_t *removed_out
) {
    if (core == NULL) {
        return ALARM_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (child_id == NULL || child_id[0] == '\0') {
        return ALARM_REMINDER_ERR_INVALID_ARGUMENT;
    }
    size_t removed = 0;
    size_t cursor = 0;
    while (cursor < core->count) {
        if (strcmp(core->alarms[cursor].child_id, child_id) == 0) {
            for (size_t shift = cursor; shift + 1 < core->count; ++shift) {
                core->alarms[shift] = core->alarms[shift + 1];
            }
            core->count -= 1;
            removed += 1;
            continue;
        }
        ++cursor;
    }
    if (removed > 0) {
        memset(&core->alarms[core->count], 0, sizeof(alarm_reminder_t));
    }
    if (removed_out != NULL) {
        *removed_out = removed;
    }
    return ALARM_REMINDER_OK;
}

size_t alarm_reminder_core_due_at(
    const alarm_reminder_core_t *core,
    int weekday,
    uint16_t minute_of_day,
    int32_t local_date,
    char (*due_ids_out)[ALARM_REMINDER_ID_SIZE],
    size_t capacity
) {
    if (core == NULL || weekday < 1 || weekday > ALARM_REMINDER_WEEKDAY_COUNT) {
        return 0;
    }
    const uint8_t weekday_bit = (uint8_t)(1u << (weekday - 1));
    size_t matched = 0;
    for (size_t index = 0; index < core->count; ++index) {
        const alarm_reminder_t *alarm = &core->alarms[index];
        if (!alarm->enabled) {
            continue;
        }
        if (alarm->minute_of_day != minute_of_day) {
            continue;
        }
        if ((alarm->weekday_mask & weekday_bit) == 0) {
            continue;
        }
        // An alarm fires at most once per local date so a persisted firing
        // state never rings again on the same day.
        if (local_date != 0 && alarm->last_fired_date == local_date) {
            continue;
        }
        if (due_ids_out != NULL && matched < capacity) {
            memcpy(
                due_ids_out[matched],
                alarm->alarm_id,
                ALARM_REMINDER_ID_SIZE
            );
        }
        matched += 1;
    }
    return matched;
}

size_t alarm_reminder_core_due_snoozes(
    const alarm_reminder_core_t *core,
    int64_t now_tick,
    char (*due_ids_out)[ALARM_REMINDER_ID_SIZE],
    size_t capacity
) {
    if (core == NULL) {
        return 0;
    }
    size_t matched = 0;
    for (size_t index = 0; index < core->count; ++index) {
        const alarm_reminder_t *alarm = &core->alarms[index];
        if (!alarm->enabled) {
            continue;
        }
        if (alarm->state != ALARM_REMINDER_STATE_SNOOZED) {
            continue;
        }
        if (alarm->snooze_deadline_tick == 0 ||
            alarm->snooze_deadline_tick > now_tick) {
            continue;
        }
        if (due_ids_out != NULL && matched < capacity) {
            memcpy(
                due_ids_out[matched],
                alarm->alarm_id,
                ALARM_REMINDER_ID_SIZE
            );
        }
        matched += 1;
    }
    return matched;
}

alarm_reminder_error_t alarm_reminder_core_set_state(
    alarm_reminder_core_t *core,
    const char *alarm_id,
    alarm_reminder_state_t state
) {
    if (core == NULL) {
        return ALARM_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (alarm_id == NULL || alarm_id[0] == '\0' ||
        state > ALARM_REMINDER_STATE_MISSED) {
        return ALARM_REMINDER_ERR_INVALID_ARGUMENT;
    }
    const size_t index = alarm_reminder_core_find(core, alarm_id);
    if (index >= core->count) {
        return ALARM_REMINDER_ERR_NOT_FOUND;
    }
    core->alarms[index].state = (uint8_t)state;
    if (state != ALARM_REMINDER_STATE_SNOOZED) {
        core->alarms[index].snooze_deadline_tick = 0;
    }
    return ALARM_REMINDER_OK;
}

alarm_reminder_error_t alarm_reminder_core_set_snooze(
    alarm_reminder_core_t *core,
    const char *alarm_id,
    int64_t deadline_tick
) {
    if (core == NULL) {
        return ALARM_REMINDER_ERR_NOT_INITIALIZED;
    }
    if (alarm_id == NULL || alarm_id[0] == '\0' || deadline_tick <= 0) {
        return ALARM_REMINDER_ERR_INVALID_ARGUMENT;
    }
    const size_t index = alarm_reminder_core_find(core, alarm_id);
    if (index >= core->count) {
        return ALARM_REMINDER_ERR_NOT_FOUND;
    }
    core->alarms[index].state = ALARM_REMINDER_STATE_SNOOZED;
    core->alarms[index].snooze_deadline_tick = deadline_tick;
    return ALARM_REMINDER_OK;
}
