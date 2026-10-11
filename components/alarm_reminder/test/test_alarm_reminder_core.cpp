#include "alarm_reminder_core.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static uint8_t weekday_mask(std::initializer_list<int> weekdays) {
    uint8_t mask = 0;
    for (int weekday : weekdays) {
        mask |= static_cast<uint8_t>(1u << (weekday - 1));
    }
    return mask;
}

static alarm_reminder_t make_alarm(
    const char *alarm_id,
    const char *child_id,
    const char *label,
    uint16_t minute_of_day,
    uint8_t mask,
    uint8_t snooze_minutes,
    bool enabled
) {
    alarm_reminder_t alarm = {};
    std::strncpy(alarm.alarm_id, alarm_id, ALARM_REMINDER_ID_SIZE - 1);
    std::strncpy(alarm.child_id, child_id, ALARM_REMINDER_CHILD_ID_SIZE - 1);
    std::strncpy(alarm.label, label, ALARM_REMINDER_LABEL_SIZE - 1);
    alarm.minute_of_day = minute_of_day;
    alarm.weekday_mask = mask;
    alarm.snooze_minutes = snooze_minutes;
    alarm.enabled = enabled;
    return alarm;
}

int main() {
    alarm_reminder_core_t core = {};
    alarm_reminder_core_init(&core);
    assert(core.count == 0);

    const alarm_reminder_t wake = make_alarm(
        "alarm_001",
        "child_001",
        "起床",
        7 * 60,
        weekday_mask({1, 2, 3, 4, 5}),
        5,
        true
    );
    assert(alarm_reminder_core_upsert(&core, &wake) == ALARM_REMINDER_OK);
    assert(core.count == 1);
    assert(std::strcmp(core.alarms[0].alarm_id, "alarm_001") == 0);

    const alarm_reminder_t nap = make_alarm(
        "alarm_002",
        "child_001",
        "午睡",
        13 * 60,
        weekday_mask({1, 2, 3, 4, 5, 6, 7}),
        20,
        true
    );
    assert(alarm_reminder_core_upsert(&core, &nap) == ALARM_REMINDER_OK);
    assert(core.count == 2);
    // Arrival order is preserved: no reshuffle on insert.
    assert(std::strcmp(core.alarms[0].alarm_id, "alarm_001") == 0);
    assert(std::strcmp(core.alarms[1].alarm_id, "alarm_002") == 0);

    // Re-upserting an existing alarm replaces it in place.
    alarm_reminder_t wake_v2 = wake;
    wake_v2.minute_of_day = 6 * 60 + 45;
    wake_v2.snooze_minutes = 10;
    wake_v2.enabled = false;
    assert(alarm_reminder_core_upsert(&core, &wake_v2) == ALARM_REMINDER_OK);
    assert(core.count == 2);
    assert(core.alarms[0].minute_of_day == 6 * 60 + 45);
    assert(core.alarms[0].snooze_minutes == 10);
    assert(!core.alarms[0].enabled);

    // Find reports the index, or the count when absent.
    assert(alarm_reminder_core_find(&core, "alarm_001") == 0);
    assert(alarm_reminder_core_find(&core, "alarm_002") == 1);
    assert(alarm_reminder_core_find(&core, "alarm_missing") == core.count);

    // Invalid alarms are rejected.
    alarm_reminder_t invalid = make_alarm(
        "",
        "child_001",
        "空编号",
        0,
        weekday_mask({1}),
        5,
        true
    );
    assert(
        alarm_reminder_core_upsert(&core, &invalid) ==
        ALARM_REMINDER_ERR_INVALID_ARGUMENT
    );
    invalid = make_alarm(
        "alarm_003",
        "child_001",
        "",
        0,
        weekday_mask({1}),
        5,
        true
    );
    assert(
        alarm_reminder_core_upsert(&core, &invalid) ==
        ALARM_REMINDER_ERR_INVALID_ARGUMENT
    );
    invalid = make_alarm(
        "alarm_003",
        "child_001",
        "无星期",
        0,
        0,
        5,
        true
    );
    assert(
        alarm_reminder_core_upsert(&core, &invalid) ==
        ALARM_REMINDER_ERR_INVALID_ARGUMENT
    );
    invalid = make_alarm(
        "alarm_003",
        "child_001",
        "越界时间",
        24 * 60,
        weekday_mask({1}),
        5,
        true
    );
    assert(
        alarm_reminder_core_upsert(&core, &invalid) ==
        ALARM_REMINDER_ERR_INVALID_ARGUMENT
    );
    invalid = make_alarm(
        "alarm_003",
        "child_001",
        "贪睡越界",
        8 * 60,
        weekday_mask({1}),
        0,
        true
    );
    assert(
        alarm_reminder_core_upsert(&core, &invalid) ==
        ALARM_REMINDER_ERR_INVALID_ARGUMENT
    );
    invalid.snooze_minutes = 31;
    assert(
        alarm_reminder_core_upsert(&core, &invalid) ==
        ALARM_REMINDER_ERR_INVALID_ARGUMENT
    );

    // Due matching: only enabled alarms with the matching minute and weekday.
    char due[ALARM_REMINDER_CAPACITY][ALARM_REMINDER_ID_SIZE] = {};
    assert(alarm_reminder_core_due_at(&core, 1, 13 * 60, 20260304, due, 16) == 1);
    assert(std::strcmp(due[0], "alarm_002") == 0);
    // alarm_001 is disabled, so Monday 06:45 matches nothing.
    assert(alarm_reminder_core_due_at(&core, 1, 6 * 60 + 45, 20260304, due, 16) == 0);
    // alarm_002 repeats every day, so Sunday also matches.
    assert(alarm_reminder_core_due_at(&core, 7, 13 * 60, 20260308, due, 16) == 1);
    // A weekday-only alarm does not match the weekend.
    alarm_reminder_core_t weekday_core = {};
    alarm_reminder_core_init(&weekday_core);
    const alarm_reminder_t weekday_only = make_alarm(
        "alarm_weekday",
        "child_001",
        "上学日",
        7 * 60,
        weekday_mask({1, 2, 3, 4, 5}),
        5,
        true
    );
    assert(
        alarm_reminder_core_upsert(&weekday_core, &weekday_only) ==
        ALARM_REMINDER_OK
    );
    assert(
        alarm_reminder_core_due_at(&weekday_core, 6, 7 * 60, 20260307, due, 16) ==
        0
    );
    // An invalid weekday reports no matches.
    assert(alarm_reminder_core_due_at(&core, 0, 13 * 60, 20260304, due, 16) == 0);

    // Once an alarm has fired on a date it does not fire again that day.
    assert(
        alarm_reminder_core_set_state(
            &core,
            "alarm_002",
            ALARM_REMINDER_STATE_DISMISSED
        ) == ALARM_REMINDER_OK
    );
    core.alarms[1].last_fired_date = 20260304;
    assert(alarm_reminder_core_due_at(&core, 1, 13 * 60, 20260304, due, 16) == 0);
    // The next day it is due again.
    assert(alarm_reminder_core_due_at(&core, 2, 13 * 60, 20260305, due, 16) == 1);

    // Snooze state and deadline round-trip.
    assert(
        alarm_reminder_core_set_snooze(
            &core,
            "alarm_002",
            alarm_reminder_core_tick(20260305, 13 * 60) + 20
        ) == ALARM_REMINDER_OK
    );
    assert(core.alarms[1].state == ALARM_REMINDER_STATE_SNOOZED);
    // Not yet due before the deadline.
    assert(
        alarm_reminder_core_due_snoozes(
            &core,
            alarm_reminder_core_tick(20260305, 13 * 60) + 19,
            due,
            16
        ) == 0
    );
    // Due exactly at the deadline.
    assert(
        alarm_reminder_core_due_snoozes(
            &core,
            alarm_reminder_core_tick(20260305, 13 * 60) + 20,
            due,
            16
        ) == 1
    );
    assert(std::strcmp(due[0], "alarm_002") == 0);
    // Dismissing clears the pending snooze deadline.
    assert(
        alarm_reminder_core_set_state(
            &core,
            "alarm_002",
            ALARM_REMINDER_STATE_DISMISSED
        ) == ALARM_REMINDER_OK
    );
    assert(core.alarms[1].snooze_deadline_tick == 0);
    assert(
        alarm_reminder_core_due_snoozes(
            &core,
            alarm_reminder_core_tick(20260305, 13 * 60) + 20,
            due,
            16
        ) == 0
    );
    // Setting state on an unknown id reports NOT_FOUND.
    assert(
        alarm_reminder_core_set_state(
            &core,
            "alarm_missing",
            ALARM_REMINDER_STATE_RINGING
        ) == ALARM_REMINDER_ERR_NOT_FOUND
    );

    // Removing a missing alarm is success.
    assert(
        alarm_reminder_core_remove(&core, "alarm_missing") ==
        ALARM_REMINDER_OK
    );
    assert(core.count == 2);

    // Removing a child drops every alarm for that child.
    size_t removed = 0;
    assert(
        alarm_reminder_core_remove_child(&core, "child_001", &removed) ==
        ALARM_REMINDER_OK
    );
    assert(removed == 2);
    assert(core.count == 0);

    // Capacity is bounded: inserting past the limit reports FULL.
    alarm_reminder_core_t full = {};
    alarm_reminder_core_init(&full);
    char id[ALARM_REMINDER_ID_SIZE];
    for (int index = 0; index < ALARM_REMINDER_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "alarm_%03d", index);
        const alarm_reminder_t alarm = make_alarm(
            id,
            "child_001",
            "闹钟",
            8 * 60,
            weekday_mask({1}),
            5,
            true
        );
        assert(alarm_reminder_core_upsert(&full, &alarm) == ALARM_REMINDER_OK);
    }
    assert(full.count == ALARM_REMINDER_CAPACITY);
    const alarm_reminder_t overflow = make_alarm(
        "alarm_overflow",
        "child_001",
        "溢出",
        8 * 60,
        weekday_mask({1}),
        5,
        true
    );
    assert(
        alarm_reminder_core_upsert(&full, &overflow) ==
        ALARM_REMINDER_ERR_FULL
    );
    assert(alarm_reminder_core_remove(&full, "alarm_000") == ALARM_REMINDER_OK);
    assert(alarm_reminder_core_upsert(&full, &overflow) == ALARM_REMINDER_OK);
    assert(full.count == ALARM_REMINDER_CAPACITY);

    // The tick helper maps dates monotonically and rejects malformed input.
    const int64_t day_one = alarm_reminder_core_tick(20260304, 0);
    const int64_t day_two = alarm_reminder_core_tick(20260305, 0);
    assert(day_one > 0);
    assert(day_two - day_one == 1440);
    // A leap day exists in 2024 but not in 2023.
    assert(
        alarm_reminder_core_tick(20240229, 0) > 0
    );
    assert(alarm_reminder_core_tick(20230229, 0) == 0);
    assert(alarm_reminder_core_tick(20261340, 0) == 0);
    assert(alarm_reminder_core_tick(20260304, 24 * 60) == 0);

    return 0;
}
