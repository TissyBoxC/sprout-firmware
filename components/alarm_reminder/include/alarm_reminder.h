#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of alarms cached on the device. */
#define ALARM_REMINDER_CAPACITY 16

/** @brief Size of an alarm id including the terminator. */
#define ALARM_REMINDER_ID_SIZE 40

/** @brief Size of a child id including the terminator. */
#define ALARM_REMINDER_CHILD_ID_SIZE 40

/** @brief Size of an alarm label including the terminator. */
#define ALARM_REMINDER_LABEL_SIZE 64

/** @brief Size of an alarm prompt including the terminator. */
#define ALARM_REMINDER_PROMPT_SIZE 256

/** @brief Number of ISO weekdays (1 = Monday ... 7 = Sunday). */
#define ALARM_REMINDER_WEEKDAY_COUNT 7

/** @brief Approved snooze bounds, matching the platform contract. */
#define ALARM_REMINDER_MIN_SNOOZE_MINUTES 1
#define ALARM_REMINDER_MAX_SNOOZE_MINUTES 30

/**
 * @brief Maximum number of unacknowledged firing events kept for the platform.
 *
 * A firing event is one alarm's latest state on one local date, so the queue is
 * keyed by (alarm id, local date) and never grows past one entry per alarm per
 * day. The bound keeps the persisted blob small while still letting a device
 * survive a reboot before it reconnects and drains the queue.
 */
#define ALARM_REMINDER_PENDING_EVENT_CAPACITY 16

/**
 * @brief One cached alarm: the daily schedule plus its local firing state.
 *
 * The schedule mirrors the platform's device alarm payload. The firing state
 * (state, last_fired_date and snooze_deadline_tick) is local bookkeeping so the
 * device rings at most once per day, can snooze, and reports the outcome back.
 */
typedef struct {
    char alarm_id[ALARM_REMINDER_ID_SIZE];
    char child_id[ALARM_REMINDER_CHILD_ID_SIZE];
    char label[ALARM_REMINDER_LABEL_SIZE];
    char prompt[ALARM_REMINDER_PROMPT_SIZE];
    /** Wall-clock alarm time in minutes past local midnight (0..1439). */
    uint16_t minute_of_day;
    /** Bitmask with bit (weekday - 1) set for each active weekday. */
    uint8_t weekday_mask;
    /** Snooze window in minutes (1..30). */
    uint8_t snooze_minutes;
    bool enabled;
    /** Local firing state (see alarm_reminder_state_t). */
    uint8_t state;
    /** Local date (YYYYMMDD) the alarm last started firing, 0 when never. */
    int32_t last_fired_date;
    /**
     * Absolute minute-tick a pending snooze expires, or 0 when none is
     * pending. See alarm_reminder_tick for the tick origin.
     */
    int64_t snooze_deadline_tick;
} alarm_reminder_t;

/** @brief Bounded local firing states, mirroring the platform catalog. */
typedef enum {
    ALARM_REMINDER_STATE_IDLE = 0,
    ALARM_REMINDER_STATE_RINGING,
    ALARM_REMINDER_STATE_SNOOZED,
    ALARM_REMINDER_STATE_DISMISSED,
    ALARM_REMINDER_STATE_MISSED,
} alarm_reminder_state_t;

/** @brief One firing outcome waiting to be reported to the platform. */
typedef struct {
    char alarm_id[ALARM_REMINDER_ID_SIZE];
    /** Local date (YYYYMMDD) the event happened on. */
    int32_t alarm_date;
    alarm_reminder_state_t state;
} alarm_reminder_pending_event_t;

/** @brief Stable alarm-reminder error codes for diagnostics and UI copy. */
typedef enum {
    ALARM_REMINDER_OK = 0,
    ALARM_REMINDER_ERR_NOT_INITIALIZED,
    ALARM_REMINDER_ERR_INVALID_ARGUMENT,
    ALARM_REMINDER_ERR_FULL,
    ALARM_REMINDER_ERR_NOT_FOUND,
    ALARM_REMINDER_ERR_STORAGE,
} alarm_reminder_error_t;

/** @brief Initialize the alarm cache from the configuration store. */
esp_err_t alarm_reminder_init(void);

/** @brief Return true when the alarm cache is ready. */
bool alarm_reminder_is_ready(void);

/** @brief Insert or replace one alarm, preserving first-seen id order. */
esp_err_t alarm_reminder_upsert(const alarm_reminder_t *alarm);

/** @brief Remove one alarm. A missing alarm is success. */
esp_err_t alarm_reminder_remove(const char *alarm_id);

/** @brief Remove every alarm for one child. Returns the removed count. */
esp_err_t alarm_reminder_remove_child(
    const char *child_id,
    size_t *removed_out
);

/**
 * @brief Replace the alarm schedule with one authoritative remote snapshot.
 *
 * The remote snapshot carries only the schedule, so the local firing state of
 * any surviving alarm id is preserved. Duplicate or unusable alarms are
 * dropped and the result is bounded to the local capacity. An empty snapshot
 * clears the schedule; a non-empty snapshot that ends up empty is rejected so
 * a malformed response cannot wipe the family schedule.
 */
esp_err_t alarm_reminder_replace_schedule(
    const alarm_reminder_t *schedule,
    size_t schedule_count
);

/** @brief Copy the cached alarms. */
esp_err_t alarm_reminder_list(
    alarm_reminder_t *alarms_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return one cached alarm by id. */
bool alarm_reminder_get(
    const char *alarm_id,
    alarm_reminder_t *alarm_out
);

/** @brief Return the current number of cached alarms. */
size_t alarm_reminder_count(void);

/**
 * @brief Return the alarms due to start ringing at one local minute.
 *
 * Only enabled alarms whose weekday mask covers [weekday] and whose time
 * matches [minute_of_day] qualify, and an alarm already fired on [local_date]
 * is skipped so it fires at most once per day. Up to [capacity] alarm ids are
 * written into [due_ids_out] when provided.
 */
size_t alarm_reminder_due_at(
    int weekday,
    uint16_t minute_of_day,
    int32_t local_date,
    char (*due_ids_out)[ALARM_REMINDER_ID_SIZE],
    size_t capacity
);

/**
 * @brief Return the snoozed alarms whose snooze window expires at now_tick.
 *
 * Up to [capacity] alarm ids are written into [due_ids_out] when provided.
 */
size_t alarm_reminder_due_snoozes(
    int64_t now_tick,
    char (*due_ids_out)[ALARM_REMINDER_ID_SIZE],
    size_t capacity
);

/**
 * @brief Convert a YYYYMMDD local date and minute of day to an absolute tick.
 *
 * The tick counts minutes since 1970-01-01 00:00 local proleptic Gregorian
 * time, so a snooze that crosses local midnight still expires correctly. An
 * invalid date reports 0.
 */
int64_t alarm_reminder_tick(int32_t local_date, uint16_t minute_of_day);

/** @brief Mark one alarm as ringing on the given local date. */
esp_err_t alarm_reminder_mark_ringing(
    const char *alarm_id,
    int32_t local_date
);

/** @brief Snooze one alarm from [local_date] and [minute_of_day]. */
esp_err_t alarm_reminder_snooze(
    const char *alarm_id,
    int32_t local_date,
    uint16_t minute_of_day
);

/** @brief Dismiss one alarm on the given local date. */
esp_err_t alarm_reminder_dismiss(const char *alarm_id, int32_t local_date);

/** @brief Mark one alarm as missed on the given local date. */
esp_err_t alarm_reminder_mark_missed(
    const char *alarm_id,
    int32_t local_date
);

/** @brief Return the number of firing events waiting to be reported. */
size_t alarm_reminder_pending_event_count(void);

/**
 * @brief Copy the firing events waiting to be reported, oldest first.
 *
 * The platform stores the latest firing state per alarm and local date, so the
 * queue keeps at most one entry per (alarm id, date) and reports the most
 * recent state.
 */
esp_err_t alarm_reminder_list_pending_events(
    alarm_reminder_pending_event_t *events_out,
    size_t capacity,
    size_t *count_out
);

/**
 * @brief Drop one reported firing event from the queue.
 *
 * Called after the platform accepts the event. A missing entry is success so a
 * retried report never fails on an already-drained queue.
 */
esp_err_t alarm_reminder_ack_event(const char *alarm_id, int32_t alarm_date);

/** @brief Return the stable string for one local firing state. */
const char *alarm_reminder_state_name(alarm_reminder_state_t state);

/** @brief Return the stable string for one alarm-reminder error code. */
const char *alarm_reminder_error_name(alarm_reminder_error_t error);

/** @brief Return the removable-module descriptor for alarm_reminder. */
const module_descriptor_t *alarm_reminder_module_descriptor(void);

#ifdef __cplusplus
}
#endif
