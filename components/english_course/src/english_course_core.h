#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "english_course.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure English course cache kept newest-change-first. */
typedef struct {
    english_course_lesson_t lessons[ENGLISH_COURSE_LESSON_CAPACITY];
    size_t lesson_count;
    english_course_attempt_t attempts[ENGLISH_COURSE_ATTEMPT_CAPACITY];
    size_t attempt_count;
} english_course_core_t;

/** @brief Reset the cache to empty. */
void english_course_core_init(english_course_core_t *core);

/** @brief Insert or replace one lesson, moving it to the head. */
english_course_error_t english_course_core_upsert_lesson(
    english_course_core_t *core,
    const english_course_lesson_t *lesson
);

/** @brief Remove one lesson; a missing lesson is success. */
english_course_error_t english_course_core_remove_lesson(
    english_course_core_t *core,
    const char *lesson_id
);

/** @brief Find one lesson index, or lesson_count when absent. */
size_t english_course_core_find_lesson(
    const english_course_core_t *core,
    const char *lesson_id
);

/**
 * @brief Append one attempt, dropping the oldest when full.
 *
 * The attempt is normalized (score clamped, passed derived) before storage.
 */
english_course_error_t english_course_core_record_attempt(
    english_course_core_t *core,
    const english_course_attempt_t *attempt
);

/** @brief Remove every attempt. */
english_course_error_t english_course_core_clear_attempts(
    english_course_core_t *core
);

/**
 * @brief Roll up the best score for one lesson's practiced items.
 *
 * Distinct items are reported in first-seen order from the newest attempts.
 * The number of distinct items is written to `count_out` even when the caller
 * passes a smaller `capacity`.
 */
english_course_error_t english_course_core_lesson_progress(
    const english_course_core_t *core,
    const char *lesson_id,
    english_course_item_progress_t *progress_out,
    size_t capacity,
    size_t *count_out
);

#ifdef __cplusplus
}
#endif
