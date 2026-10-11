#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of habit reminders cached on the device. */
#define HABIT_REMINDER_CAPACITY 16

/** @brief Size of a reminder id including the terminator. */
#define HABIT_REMINDER_ID_SIZE 40

/** @brief Size of a child id including the terminator. */
#define HABIT_REMINDER_CHILD_ID_SIZE 40

/** @brief Size of a reminder label including the terminator. */
#define HABIT_REMINDER_LABEL_SIZE 64

/** @brief Size of a reminder prompt including the terminator. */
#define HABIT_REMINDER_PROMPT_SIZE 224

/** @brief Number of ISO weekdays (1 = Monday ... 7 = Sunday). */
#define HABIT_REMINDER_WEEKDAY_COUNT 7

/** @brief One cached habit reminder. */
typedef struct {
    char reminder_id[HABIT_REMINDER_ID_SIZE];
    char child_id[HABIT_REMINDER_CHILD_ID_SIZE];
    char label[HABIT_REMINDER_LABEL_SIZE];
    char prompt[HABIT_REMINDER_PROMPT_SIZE];
    /** Wall-clock reminder time in minutes past local midnight (0..1439). */
    uint16_t minute_of_day;
    /** Bitmask with bit (weekday - 1) set for each active weekday. */
    uint8_t weekday_mask;
    bool enabled;
} habit_reminder_t;

/** @brief Stable habit-reminder error codes for diagnostics and UI copy. */
typedef enum {
    HABIT_REMINDER_OK = 0,
    HABIT_REMINDER_ERR_NOT_INITIALIZED,
    HABIT_REMINDER_ERR_INVALID_ARGUMENT,
    HABIT_REMINDER_ERR_FULL,
    HABIT_REMINDER_ERR_NOT_FOUND,
    HABIT_REMINDER_ERR_STORAGE,
} habit_reminder_error_t;

/** @brief Initialize the habit reminder cache from the configuration store. */
esp_err_t habit_reminder_init(void);

/** @brief Return true when the habit reminder cache is ready. */
bool habit_reminder_is_ready(void);

/** @brief Insert or replace one reminder, preserving id order of arrival. */
esp_err_t habit_reminder_upsert(const habit_reminder_t *reminder);

/** @brief Remove one reminder. A missing reminder is success. */
esp_err_t habit_reminder_remove(const char *reminder_id);

/** @brief Remove every reminder for one child. Returns the removed count. */
esp_err_t habit_reminder_remove_child(
    const char *child_id,
    size_t *removed_out
);

/** @brief Copy the cached reminders. */
esp_err_t habit_reminder_list(
    habit_reminder_t *reminders_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return one cached reminder by id. */
bool habit_reminder_get(
    const char *reminder_id,
    habit_reminder_t *reminder_out
);

/** @brief Return the current number of cached reminders. */
size_t habit_reminder_count(void);

/**
 * @brief Return the bitmask of reminders due at one local minute.
 *
 * The caller passes the ISO weekday (1..7) and the local minute of day; the
 * function returns the number of enabled reminders that match, and writes up
 * to capacity reminder ids into due_ids_out when provided.
 */
size_t habit_reminder_due_at(
    int weekday,
    uint16_t minute_of_day,
    char (*due_ids_out)[HABIT_REMINDER_ID_SIZE],
    size_t capacity
);

/** @brief Return the stable string for one habit-reminder error code. */
const char *habit_reminder_error_name(habit_reminder_error_t error);

/** @brief Return the removable-module descriptor for habit_reminder. */
const module_descriptor_t *habit_reminder_module_descriptor(void);

#ifdef __cplusplus
}
#endif
