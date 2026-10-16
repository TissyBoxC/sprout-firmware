#include "alarm_reminder_sync_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static alarm_reminder_t make_alarm(
    const char *alarm_id,
    const char *label,
    uint16_t minute_of_day,
    uint8_t weekday_mask
) {
    alarm_reminder_t alarm = {};
    std::strncpy(alarm.alarm_id, alarm_id, ALARM_REMINDER_ID_SIZE - 1);
    std::strncpy(alarm.child_id, "child_001", ALARM_REMINDER_CHILD_ID_SIZE - 1);
    std::strncpy(alarm.label, label, ALARM_REMINDER_LABEL_SIZE - 1);
    alarm.minute_of_day = minute_of_day;
    alarm.weekday_mask = weekday_mask;
    alarm.snooze_minutes = 5;
    alarm.enabled = true;
    return alarm;
}

int main() {
    alarm_reminder_core_t core = {};
    alarm_reminder_core_init(&core);
    alarm_reminder_t local = make_alarm("alarm_morning", "起床", 420, 0x7F);
    assert(alarm_reminder_core_upsert(&core, &local) == ALARM_REMINDER_OK);
    // Seed a local firing state that a schedule refresh must preserve.
    assert(
        alarm_reminder_core_set_state(
            &core,
            "alarm_morning",
            ALARM_REMINDER_STATE_DISMISSED
        ) == ALARM_REMINDER_OK
    );
    core.alarms[0].last_fired_date = 20261013;

    alarm_reminder_t remote[] = {
        make_alarm("alarm_morning", "起床啦", 425, 0x1F),
        make_alarm("alarm_bedtime", "睡觉", 1230, 0x7F),
        // Duplicate id is dropped.
        make_alarm("alarm_bedtime", "重复", 100, 0x7F),
    };
    assert(
        alarm_reminder_sync_core_replace_schedule(&core, remote, 3) ==
        ALARM_REMINDER_OK
    );
    assert(core.count == 2);
    // The refreshed schedule is applied...
    assert(std::strcmp(core.alarms[0].alarm_id, "alarm_morning") == 0);
    assert(std::strcmp(core.alarms[0].label, "起床啦") == 0);
    assert(core.alarms[0].minute_of_day == 425);
    // ...but the surviving alarm keeps its local firing state.
    assert(core.alarms[0].state == ALARM_REMINDER_STATE_DISMISSED);
    assert(core.alarms[0].last_fired_date == 20261013);
    assert(std::strcmp(core.alarms[1].alarm_id, "alarm_bedtime") == 0);

    // An empty valid remote snapshot is authoritative and clears the schedule.
    assert(
        alarm_reminder_core_record_event(
            &core,
            "alarm_morning",
            20261014,
            ALARM_REMINDER_STATE_DISMISSED
        ) == ALARM_REMINDER_OK
    );
    assert(core.pending_event_count == 1);
    assert(
        alarm_reminder_sync_core_replace_schedule(&core, nullptr, 0) ==
        ALARM_REMINDER_OK
    );
    assert(core.count == 0);
    // Clearing the schedule must not discard unacknowledged firing outcomes.
    assert(core.pending_event_count == 1);
    assert(std::strcmp(core.pending_events[0].alarm_id, "alarm_morning") == 0);

    // Invalid-only snapshots are rejected without touching the schedule.
    const alarm_reminder_t valid = make_alarm(
        "alarm_bedtime",
        "睡觉",
        1230,
        0x7F
    );
    assert(alarm_reminder_core_upsert(&core, &valid) == ALARM_REMINDER_OK);
    alarm_reminder_t invalid_only[] = {
        make_alarm("", "无编号", 100, 0x7F),
        make_alarm("alarm_bad", "无星期", 100, 0),
    };
    assert(
        alarm_reminder_sync_core_replace_schedule(&core, invalid_only, 2) ==
        ALARM_REMINDER_ERR_INVALID_ARGUMENT
    );
    assert(core.count == 1);
    assert(std::strcmp(core.alarms[0].alarm_id, "alarm_bedtime") == 0);

    // Capacity is bounded without rejecting the whole snapshot.
    alarm_reminder_t too_many[ALARM_REMINDER_CAPACITY + 2] = {};
    char ids[ALARM_REMINDER_CAPACITY + 2][ALARM_REMINDER_ID_SIZE] = {};
    for (int index = 0; index < ALARM_REMINDER_CAPACITY + 2; ++index) {
        std::snprintf(ids[index], sizeof(ids[index]), "alarm_%03d", index);
        too_many[index] = make_alarm(ids[index], "提醒", 100, 0x7F);
    }
    assert(
        alarm_reminder_sync_core_replace_schedule(
            &core,
            too_many,
            ALARM_REMINDER_CAPACITY + 2
        ) == ALARM_REMINDER_OK
    );
    assert(core.count == ALARM_REMINDER_CAPACITY);
    assert(std::strcmp(core.alarms[0].alarm_id, "alarm_000") == 0);

    return 0;
}
