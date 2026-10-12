#pragma once

#include <stddef.h>

#include "english_course_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Replace the cached lesson catalog with one remote snapshot.
 *
 * Remote lessons are applied in order, dropping duplicate lesson ids and any
 * lesson whose ids or required text are unusable, and bounded to the local
 * capacity. The local practice-attempt log is deliberately preserved so a
 * catalog refresh never erases a child's pronunciation scores. An empty remote
 * snapshot clears the catalog. A malformed snapshot leaves the catalog
 * unchanged, so a transient provider error cannot wipe the teaching content.
 */
english_course_error_t english_course_sync_core_replace_lessons(
    english_course_core_t *core,
    const english_course_lesson_t *lessons,
    size_t lesson_count
);

#ifdef __cplusplus
}
#endif
