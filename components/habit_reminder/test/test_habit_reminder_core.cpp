#include "habit_reminder_core.h"

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

static habit_reminder_t make_reminder(
    const char *reminder_id,
    const char *child_id,
    const char *label,
    uint16_t minute_of_day,
    uint8_t mask,
    bool enabled
) {
    habit_reminder_t reminder = {};
    std::strncpy(
        reminder.reminder_id,
        reminder_id,
        HABIT_REMINDER_ID_SIZE - 1
    );
    std::strncpy(reminder.child_id, child_id, HABIT_REMINDER_CHILD_ID_SIZE - 1);
    std::strncpy(reminder.label, label, HABIT_REMINDER_LABEL_SIZE - 1);
    reminder.minute_of_day = minute_of_day;
    reminder.weekday_mask = mask;
    reminder.enabled = enabled;
    return reminder;
}

int main() {
    habit_reminder_core_t core = {};
    habit_reminder_core_init(&core);
    assert(core.count == 0);

    const habit_reminder_t brush = make_reminder(
        "reminder_001",
        "child_001",
        "刷牙",
        7 * 60 + 30,
        weekday_mask({1, 3, 5}),
        true
    );
    assert(habit_reminder_core_upsert(&core, &brush) == HABIT_REMINDER_OK);
    assert(core.count == 1);
    assert(std::strcmp(core.reminders[0].reminder_id, "reminder_001") == 0);

    const habit_reminder_t water = make_reminder(
        "reminder_002",
        "child_001",
        "喝水",
        9 * 60,
        weekday_mask({1, 2, 3, 4, 5, 6, 7}),
        true
    );
    assert(habit_reminder_core_upsert(&core, &water) == HABIT_REMINDER_OK);
    assert(core.count == 2);
    // Arrival order is preserved: no reshuffle on insert.
    assert(std::strcmp(core.reminders[0].reminder_id, "reminder_001") == 0);
    assert(std::strcmp(core.reminders[1].reminder_id, "reminder_002") == 0);

    // Re-upserting an existing reminder replaces it in place.
    habit_reminder_t brush_v2 = brush;
    brush_v2.minute_of_day = 20 * 60;
    brush_v2.enabled = false;
    assert(habit_reminder_core_upsert(&core, &brush_v2) == HABIT_REMINDER_OK);
    assert(core.count == 2);
    assert(core.reminders[0].minute_of_day == 20 * 60);
    assert(!core.reminders[0].enabled);

    // Find reports the index, or the count when absent.
    assert(habit_reminder_core_find(&core, "reminder_001") == 0);
    assert(habit_reminder_core_find(&core, "reminder_002") == 1);
    assert(habit_reminder_core_find(&core, "reminder_missing") == core.count);

    // Invalid reminders are rejected.
    habit_reminder_t invalid = make_reminder(
        "",
        "child_001",
        "空编号",
        0,
        weekday_mask({1}),
        true
    );
    assert(
        habit_reminder_core_upsert(&core, &invalid) ==
        HABIT_REMINDER_ERR_INVALID_ARGUMENT
    );
    invalid = make_reminder(
        "reminder_003",
        "child_001",
        "",
        0,
        weekday_mask({1}),
        true
    );
    assert(
        habit_reminder_core_upsert(&core, &invalid) ==
        HABIT_REMINDER_ERR_INVALID_ARGUMENT
    );
    invalid = make_reminder(
        "reminder_003",
        "child_001",
        "无星期",
        0,
        0,
        true
    );
    assert(
        habit_reminder_core_upsert(&core, &invalid) ==
        HABIT_REMINDER_ERR_INVALID_ARGUMENT
    );
    invalid = make_reminder(
        "reminder_003",
        "child_001",
        "越界时间",
        24 * 60,
        weekday_mask({1}),
        true
    );
    assert(
        habit_reminder_core_upsert(&core, &invalid) ==
        HABIT_REMINDER_ERR_INVALID_ARGUMENT
    );

    // Due matching: only enabled reminders with the matching minute and
    // weekday qualify.
    char due[HABIT_REMINDER_CAPACITY][HABIT_REMINDER_ID_SIZE] = {};
    assert(habit_reminder_core_due_at(&core, 1, 9 * 60, due, 16) == 1);
    assert(std::strcmp(due[0], "reminder_002") == 0);
    // reminder_001 is disabled, so Monday 20:00 matches nothing.
    assert(habit_reminder_core_due_at(&core, 1, 20 * 60, due, 16) == 0);
    // Tuesday 09:00 still matches the daily reminder.
    assert(habit_reminder_core_due_at(&core, 2, 9 * 60, due, 16) == 1);
    // An invalid weekday reports no matches.
    assert(habit_reminder_core_due_at(&core, 0, 9 * 60, due, 16) == 0);

    // Removing a missing reminder is success.
    assert(
        habit_reminder_core_remove(&core, "reminder_missing") ==
        HABIT_REMINDER_OK
    );
    assert(core.count == 2);

    // Removing a child drops every reminder for that child.
    size_t removed = 0;
    assert(
        habit_reminder_core_remove_child(&core, "child_001", &removed) ==
        HABIT_REMINDER_OK
    );
    assert(removed == 2);
    assert(core.count == 0);

    // Capacity is bounded: inserting past the limit reports FULL.
    habit_reminder_core_t full = {};
    habit_reminder_core_init(&full);
    char id[HABIT_REMINDER_ID_SIZE];
    for (int index = 0; index < HABIT_REMINDER_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "reminder_%03d", index);
        const habit_reminder_t reminder = make_reminder(
            id,
            "child_001",
            "习惯",
            8 * 60,
            weekday_mask({1}),
            true
        );
        assert(habit_reminder_core_upsert(&full, &reminder) == HABIT_REMINDER_OK);
    }
    assert(full.count == HABIT_REMINDER_CAPACITY);
    const habit_reminder_t overflow = make_reminder(
        "reminder_overflow",
        "child_001",
        "溢出",
        8 * 60,
        weekday_mask({1}),
        true
    );
    assert(
        habit_reminder_core_upsert(&full, &overflow) == HABIT_REMINDER_ERR_FULL
    );
    assert(habit_reminder_core_remove(&full, "reminder_000") == HABIT_REMINDER_OK);
    assert(habit_reminder_core_upsert(&full, &overflow) == HABIT_REMINDER_OK);
    assert(full.count == HABIT_REMINDER_CAPACITY);

    return 0;
}
