#include "assessment_sync_core.h"

#include <string.h>

// A score outside this range is refused rather than clamped, because a clamped
// value would report a performance the guardian never gave. The platform
// enforces the same bounds and answers 422 outside them.
#define ASSESSMENT_SYNC_SCORE_MIN 0
#define ASSESSMENT_SYNC_SCORE_MAX 100

// The platform bounds an item position to 0..9 and a difficulty to 1..5. A
// response outside either range means the payload is not an assessment the
// device understands, so it is rejected instead of rendered.
#define ASSESSMENT_SYNC_ITEM_POSITION_MAX 9
#define ASSESSMENT_SYNC_DIFFICULTY_MIN 1
#define ASSESSMENT_SYNC_DIFFICULTY_MAX 5
#define ASSESSMENT_SYNC_COUNT_MIN 0
#define ASSESSMENT_SYNC_COUNT_MAX 10
#define ASSESSMENT_SYNC_PERCENT_MIN 0
#define ASSESSMENT_SYNC_PERCENT_MAX 100

static bool assessment_sync_copy_text(
    char *destination,
    size_t destination_size,
    const char *value
) {
    if (destination == NULL || value == NULL || destination_size == 0) {
        return false;
    }
    const size_t length = strlen(value);
    if (length == 0 || length >= destination_size) {
        return false;
    }
    memcpy(destination, value, length + 1);
    return true;
}

static bool assessment_sync_is_known_state(const char *state) {
    return strcmp(state, "active") == 0 || strcmp(state, "completed") == 0 ||
        strcmp(state, "abandoned") == 0;
}

bool assessment_sync_item_type_is_valid(const char *item_type) {
    if (item_type == NULL) {
        return false;
    }
    return strcmp(item_type, "word") == 0 || strcmp(item_type, "sentence") == 0;
}

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
) {
    if (session_out == NULL) {
        return false;
    }
    memset(session_out, 0, sizeof(*session_out));
    if (!assessment_sync_copy_text(
            session_out->session_id,
            sizeof(session_out->session_id),
            session_id
        ) ||
        !assessment_sync_copy_text(
            session_out->state,
            sizeof(session_out->state),
            state
        ) ||
        !assessment_sync_is_known_state(session_out->state)) {
        return false;
    }
    if (difficulty < ASSESSMENT_SYNC_DIFFICULTY_MIN ||
        difficulty > ASSESSMENT_SYNC_DIFFICULTY_MAX ||
        answered_count < ASSESSMENT_SYNC_COUNT_MIN ||
        answered_count > ASSESSMENT_SYNC_COUNT_MAX ||
        correct_count < ASSESSMENT_SYNC_COUNT_MIN ||
        correct_count > ASSESSMENT_SYNC_COUNT_MAX ||
        mastery < ASSESSMENT_SYNC_PERCENT_MIN ||
        mastery > ASSESSMENT_SYNC_PERCENT_MAX) {
        return false;
    }
    session_out->accepts_answers = accepts_answers;
    session_out->difficulty = difficulty;
    session_out->answered_count = answered_count;
    session_out->correct_count = correct_count;
    session_out->mastery = mastery;
    if (!has_item) {
        return true;
    }
    if (item_position < 0 || item_position > ASSESSMENT_SYNC_ITEM_POSITION_MAX) {
        return false;
    }
    // An item the device cannot render is dropped rather than half-filled: the
    // child must never see a question with no target text.
    if (!assessment_sync_copy_text(
            session_out->item.item_id,
            sizeof(session_out->item.item_id),
            item_id
        ) ||
        !assessment_sync_copy_text(
            session_out->item.item_type,
            sizeof(session_out->item.item_type),
            item_type
        ) ||
        !assessment_sync_item_type_is_valid(session_out->item.item_type) ||
        !assessment_sync_copy_text(
            session_out->item.text,
            sizeof(session_out->item.text),
            item_text
        )) {
        return false;
    }
    if (item_difficulty < ASSESSMENT_SYNC_DIFFICULTY_MIN ||
        item_difficulty > ASSESSMENT_SYNC_DIFFICULTY_MAX) {
        return false;
    }
    session_out->item.has_item = true;
    session_out->item.position = item_position;
    session_out->item.difficulty = item_difficulty;
    return true;
}

bool assessment_sync_answer_from_values(
    assessment_sync_answer_t *answer_out,
    const char *item_id,
    const char *item_type,
    const char *target_text,
    int score
) {
    if (answer_out == NULL) {
        return false;
    }
    memset(answer_out, 0, sizeof(*answer_out));
    if (score < ASSESSMENT_SYNC_SCORE_MIN || score > ASSESSMENT_SYNC_SCORE_MAX) {
        return false;
    }
    if (!assessment_sync_copy_text(
            answer_out->item_id,
            sizeof(answer_out->item_id),
            item_id
        ) ||
        !assessment_sync_copy_text(
            answer_out->item_type,
            sizeof(answer_out->item_type),
            item_type
        ) ||
        !assessment_sync_item_type_is_valid(answer_out->item_type) ||
        !assessment_sync_copy_text(
            answer_out->target_text,
            sizeof(answer_out->target_text),
            target_text
        )) {
        return false;
    }
    answer_out->score = score;
    return true;
}

bool assessment_sync_answer_find(
    const assessment_sync_answer_t *answers,
    size_t answer_count,
    const char *item_id
) {
    if (answers == NULL || item_id == NULL) {
        return false;
    }
    for (size_t index = 0; index < answer_count; ++index) {
        if (strcmp(answers[index].item_id, item_id) == 0) {
            return true;
        }
    }
    return false;
}

bool assessment_sync_answer_push(
    assessment_sync_answer_t *answers,
    size_t capacity,
    size_t *answer_count_in_out,
    const assessment_sync_answer_t *answer
) {
    if (answers == NULL || answer_count_in_out == NULL || answer == NULL) {
        return false;
    }
    if (*answer_count_in_out >= capacity) {
        return false;
    }
    if (assessment_sync_answer_find(answers, *answer_count_in_out, answer->item_id)) {
        return false;
    }
    answers[*answer_count_in_out] = *answer;
    *answer_count_in_out += 1;
    return true;
}

void assessment_sync_answer_pop(
    assessment_sync_answer_t *answers,
    size_t *answer_count_in_out
) {
    if (answers == NULL || answer_count_in_out == NULL ||
        *answer_count_in_out == 0) {
        return;
    }
    // The queue is drained from the head in report order, so a refused answer
    // stops the drain and the order never changes between attempts.
    if (*answer_count_in_out > 1) {
        memmove(
            &answers[0],
            &answers[1],
            (*answer_count_in_out - 1) * sizeof(answers[0])
        );
    }
    *answer_count_in_out -= 1;
    memset(
        &answers[*answer_count_in_out],
        0,
        sizeof(answers[*answer_count_in_out])
    );
}

bool assessment_sync_answer_matches_item(
    const assessment_sync_item_t *item,
    const char *item_id,
    const char *item_type,
    const char *target_text
) {
    if (item == NULL || !item->has_item || item_id == NULL ||
        item_type == NULL || target_text == NULL) {
        return false;
    }
    // The target text is part of the identity because the platform validates it
    // against the course item; a mismatch would be rejected with 422.
    return strcmp(item->item_id, item_id) == 0 &&
        strcmp(item->item_type, item_type) == 0 &&
        strcmp(item->text, target_text) == 0;
}

uint32_t assessment_sync_backoff_seconds(
    uint32_t base_seconds,
    uint32_t max_seconds,
    uint32_t failure_count
) {
    if (base_seconds == 0) {
        return max_seconds;
    }
    uint32_t delay = base_seconds;
    // Doubling is capped before it can overflow, so a long outage cannot wrap
    // the delay back to zero and turn the worker into a request loop.
    for (uint32_t attempt = 0; attempt < failure_count; ++attempt) {
        if (delay >= max_seconds) {
            return max_seconds;
        }
        delay *= 2;
    }
    return delay > max_seconds ? max_seconds : delay;
}
