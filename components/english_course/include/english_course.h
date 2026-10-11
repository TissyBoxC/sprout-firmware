#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of lessons cached on the device. */
#define ENGLISH_COURSE_LESSON_CAPACITY 12

/** @brief Maximum number of words kept per lesson. */
#define ENGLISH_COURSE_WORDS_PER_LESSON 8

/** @brief Maximum number of sentence patterns kept per lesson. */
#define ENGLISH_COURSE_SENTENCES_PER_LESSON 4

/** @brief Maximum number of practice attempts kept for progress. */
#define ENGLISH_COURSE_ATTEMPT_CAPACITY 32

/** @brief Size of a lesson id including the terminator. */
#define ENGLISH_COURSE_ID_SIZE 40

/** @brief Size of a word or sentence item id including the terminator. */
#define ENGLISH_COURSE_ITEM_ID_SIZE 40

/** @brief Size of a short text field (term, phonetic, translation). */
#define ENGLISH_COURSE_TEXT_SIZE 64

/** @brief Size of a long text field (example, pattern, model answer). */
#define ENGLISH_COURSE_LONG_TEXT_SIZE 160

/** @brief Score (0-100) at or above which one attempt counts as passed. */
#define ENGLISH_COURSE_PASSING_SCORE 60

/**
 * @brief Practice item kinds.
 *
 * The numeric values are stable because they cross the persisted blob; keep
 * them in sync with the platform's `word` and `sentence`.
 */
typedef enum {
    ENGLISH_ITEM_WORD = 0,
    ENGLISH_ITEM_SENTENCE = 1,
} english_course_item_type_t;

/** @brief One cached vocabulary word. */
typedef struct {
    char item_id[ENGLISH_COURSE_ITEM_ID_SIZE];
    char term[ENGLISH_COURSE_TEXT_SIZE];
    char phonetic[ENGLISH_COURSE_TEXT_SIZE];
    char translation[ENGLISH_COURSE_TEXT_SIZE];
    char example[ENGLISH_COURSE_LONG_TEXT_SIZE];
} english_course_word_t;

/** @brief One cached spoken sentence pattern. */
typedef struct {
    char item_id[ENGLISH_COURSE_ITEM_ID_SIZE];
    char pattern[ENGLISH_COURSE_LONG_TEXT_SIZE];
    char prompt[ENGLISH_COURSE_LONG_TEXT_SIZE];
    char model_answer[ENGLISH_COURSE_LONG_TEXT_SIZE];
} english_course_sentence_t;

/** @brief One cached lesson summary plus its ordered items. */
typedef struct {
    char lesson_id[ENGLISH_COURSE_ID_SIZE];
    char title[ENGLISH_COURSE_TEXT_SIZE];
    char age_tier[ENGLISH_COURSE_TEXT_SIZE];
    size_t word_count;
    size_t sentence_count;
    english_course_word_t words[ENGLISH_COURSE_WORDS_PER_LESSON];
    english_course_sentence_t sentences[ENGLISH_COURSE_SENTENCES_PER_LESSON];
} english_course_lesson_t;

/** @brief One locally recorded pronunciation-feedback attempt. */
typedef struct {
    char lesson_id[ENGLISH_COURSE_ID_SIZE];
    char item_id[ENGLISH_COURSE_ITEM_ID_SIZE];
    english_course_item_type_t item_type;
    int score;
    bool passed;
    int64_t practiced_at_epoch_seconds;
} english_course_attempt_t;

/** @brief One guardian-visible best-score rollup for an item. */
typedef struct {
    char lesson_id[ENGLISH_COURSE_ID_SIZE];
    char item_id[ENGLISH_COURSE_ITEM_ID_SIZE];
    english_course_item_type_t item_type;
    int best_score;
    size_t attempts;
    bool passed;
} english_course_item_progress_t;

/** @brief Stable English course error codes for diagnostics and UI copy. */
typedef enum {
    ENGLISH_COURSE_OK = 0,
    ENGLISH_COURSE_ERR_NOT_INITIALIZED,
    ENGLISH_COURSE_ERR_INVALID_ARGUMENT,
    ENGLISH_COURSE_ERR_FULL,
    ENGLISH_COURSE_ERR_NOT_FOUND,
    ENGLISH_COURSE_ERR_STORAGE,
} english_course_error_t;

/** @brief Initialize the English course cache from the configuration store. */
esp_err_t english_course_init(void);

/** @brief Return true when the English course cache is ready. */
bool english_course_is_ready(void);

/** @brief Insert or replace one lesson, newest change first. */
esp_err_t english_course_upsert_lesson(const english_course_lesson_t *lesson);

/** @brief Remove one lesson. A missing lesson is success. */
esp_err_t english_course_remove_lesson(const char *lesson_id);

/** @brief Copy the cached lesson summaries newest-first. */
esp_err_t english_course_list_lessons(
    english_course_lesson_t *lessons_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return one cached lesson by id. */
bool english_course_get_lesson(
    const char *lesson_id,
    english_course_lesson_t *lesson_out
);

/** @brief Return the current number of cached lessons. */
size_t english_course_lesson_count(void);

/**
 * @brief Record one practice attempt.
 *
 * The score is clamped to 0-100 because a noisy microphone can report an
 * out-of-range value; `passed` is derived from the clamped score so a caller
 * cannot claim a pass it did not earn.
 */
esp_err_t english_course_record_attempt(
    const english_course_attempt_t *attempt
);

/** @brief Copy recorded attempts newest-first. */
esp_err_t english_course_list_attempts(
    english_course_attempt_t *attempts_out,
    size_t capacity,
    size_t *count_out
);

/**
 * @brief Roll up the best score for one lesson's items.
 *
 * Returns the number of distinct items with at least one attempt, copying up
 * to `capacity` rollups. A lesson with no attempts reports zero.
 */
esp_err_t english_course_lesson_progress(
    const char *lesson_id,
    english_course_item_progress_t *progress_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return the current number of recorded attempts. */
size_t english_course_attempt_count(void);

/** @brief Remove every attempt, e.g. when a guardian clears a child's history. */
esp_err_t english_course_clear_attempts(void);

/** @brief Return the stable string for one item type. */
const char *english_course_item_type_name(english_course_item_type_t type);

/** @brief Parse a stable item type string, or return false when unknown. */
bool english_course_item_type_from_name(
    const char *name,
    english_course_item_type_t *type_out
);

/** @brief Return the stable string for one English course error code. */
const char *english_course_error_name(english_course_error_t error);

/** @brief Return the removable-module descriptor for english_course. */
const module_descriptor_t *english_course_module_descriptor(void);

#ifdef __cplusplus
}
#endif
