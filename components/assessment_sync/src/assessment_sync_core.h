#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "assessment_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One answer the guardian graded, waiting to be reported.
 *
 * The score is carried verbatim from the guardian; the device never computes
 * it.
 */
typedef struct {
    char item_id[ASSESSMENT_SYNC_ITEM_ID_SIZE];
    char item_type[ASSESSMENT_SYNC_ITEM_TYPE_SIZE];
    char target_text[ASSESSMENT_SYNC_TARGET_TEXT_SIZE];
    int32_t score;
} assessment_sync_answer_t;

/**
 * @brief Parsed assessment session as delivered by the platform.
 *
 * Everything here comes from the device session endpoint. `child_id` is
 * deliberately absent: the device never needs it to answer, and keeping it out
 * of the component means it cannot reach a log line.
 */
typedef struct {
    char session_id[ASSESSMENT_SYNC_SESSION_ID_SIZE];
    char state[ASSESSMENT_SYNC_STATE_SIZE];
    bool accepts_answers;
    int32_t difficulty;
    int32_t answered_count;
    int32_t correct_count;
    int32_t mastery;
    assessment_sync_item_t item;
} assessment_sync_session_t;

/**
 * @brief Parse the `data` object of a device session response.
 *
 * `next_item` is read whenever the platform sends it; a session that still
 * accepts answers without an item leaves `item.has_item` false, so the caller
 * presents nothing rather than inventing a question.
 *
 * @return false when the payload cannot be trusted: a missing or oversized
 *         session id, an unknown state, or an item with an unusable type or
 *         empty text. The caller then keeps its previous snapshot.
 */
bool assessment_sync_parse_session(
    assessment_sync_session_t *session_out,
    const char *session_id,
    const char *state,
    bool accepts_answers,
    int32_t difficulty,
    int32_t answered_count,
    int32_t correct_count,
    int32_t mastery,
    bool has_item,
    int32_t item_position,
    const char *item_id,
    const char *item_type,
    const char *item_text,
    int32_t item_difficulty
);

/** @brief Return true when the type is one the platform accepts. */
bool assessment_sync_item_type_is_valid(const char *item_type);

/**
 * @brief Build a queued answer from the values the guardian supplied.
 *
 * Fails when any required field is unusable or the score is outside 0-100.
 * An out-of-range score is refused instead of clamped: a clamped score would
 * claim a performance the guardian never reported.
 */
bool assessment_sync_answer_from_values(
    assessment_sync_answer_t *answer_out,
    const char *item_id,
    const char *item_type,
    const char *target_text,
    int score
);

/** @brief Return true when one item id already sits in the queue. */
bool assessment_sync_answer_find(
    const assessment_sync_answer_t *answers,
    size_t answer_count,
    const char *item_id
);

/**
 * @brief Append one answer, keeping the queue bounded.
 *
 * A duplicate item id is refused rather than stored twice: the platform
 * consumes each position exactly once, so a duplicated row would report an
 * answer the child never gave. Returns false when the queue is full or the
 * answer is a duplicate; the caller reports the failure instead of dropping
 * the answer silently.
 */
bool assessment_sync_answer_push(
    assessment_sync_answer_t *answers,
    size_t capacity,
    size_t *answer_count_in_out,
    const assessment_sync_answer_t *answer
);

/** @brief Remove the head of the queue after the platform accepted it. */
void assessment_sync_answer_pop(
    assessment_sync_answer_t *answers,
    size_t *answer_count_in_out
);

/**
 * @brief Return true when the answer belongs to the tracked session item.
 *
 * The guardian may grade on their phone while the device has already moved to
 * the next item, so only answers for the item currently shown are accepted.
 */
bool assessment_sync_answer_matches_item(
    const assessment_sync_item_t *item,
    const char *item_id,
    const char *item_type,
    const char *target_text
);

/**
 * @brief Delay before the next attempt after `failure_count` failures.
 *
 * Growth is exponential from the configured base and capped, so a backend
 * outage produces a handful of retries instead of a request storm, and a
 * healthy device returns to the normal interval immediately.
 */
uint32_t assessment_sync_backoff_seconds(
    uint32_t base_seconds,
    uint32_t max_seconds,
    uint32_t failure_count
);

#ifdef __cplusplus
}
#endif
