#include "english_course_core.h"

#include <string.h>

void english_course_core_init(english_course_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

static bool english_course_item_type_is_valid(english_course_item_type_t type) {
    return type == ENGLISH_ITEM_WORD || type == ENGLISH_ITEM_SENTENCE;
}

static bool english_course_lesson_is_valid(
    const english_course_lesson_t *lesson
) {
    if (lesson == NULL) {
        return false;
    }
    if (lesson->lesson_id[0] == '\0') {
        return false;
    }
    if (lesson->word_count > ENGLISH_COURSE_WORDS_PER_LESSON) {
        return false;
    }
    if (lesson->sentence_count > ENGLISH_COURSE_SENTENCES_PER_LESSON) {
        return false;
    }
    for (size_t index = 0; index < lesson->word_count; ++index) {
        if (lesson->words[index].item_id[0] == '\0' ||
            lesson->words[index].term[0] == '\0') {
            return false;
        }
    }
    for (size_t index = 0; index < lesson->sentence_count; ++index) {
        if (lesson->sentences[index].item_id[0] == '\0' ||
            lesson->sentences[index].pattern[0] == '\0') {
            return false;
        }
    }
    return true;
}

size_t english_course_core_find_lesson(
    const english_course_core_t *core,
    const char *lesson_id
) {
    if (core == NULL || lesson_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->lesson_count; ++index) {
        if (strcmp(core->lessons[index].lesson_id, lesson_id) == 0) {
            return index;
        }
    }
    return core->lesson_count;
}

english_course_error_t english_course_core_upsert_lesson(
    english_course_core_t *core,
    const english_course_lesson_t *lesson
) {
    if (core == NULL) {
        return ENGLISH_COURSE_ERR_NOT_INITIALIZED;
    }
    if (!english_course_lesson_is_valid(lesson)) {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = english_course_core_find_lesson(
        core,
        lesson->lesson_id
    );
    if (existing >= core->lesson_count &&
        core->lesson_count >= ENGLISH_COURSE_LESSON_CAPACITY) {
        return ENGLISH_COURSE_ERR_FULL;
    }
    if (existing < core->lesson_count) {
        for (size_t index = existing; index > 0; --index) {
            core->lessons[index] = core->lessons[index - 1];
        }
        core->lessons[0] = *lesson;
        return ENGLISH_COURSE_OK;
    }
    const size_t last = core->lesson_count < ENGLISH_COURSE_LESSON_CAPACITY
        ? core->lesson_count
        : ENGLISH_COURSE_LESSON_CAPACITY - 1;
    for (size_t index = last; index > 0; --index) {
        core->lessons[index] = core->lessons[index - 1];
    }
    core->lessons[0] = *lesson;
    core->lesson_count += 1;
    return ENGLISH_COURSE_OK;
}

english_course_error_t english_course_core_remove_lesson(
    english_course_core_t *core,
    const char *lesson_id
) {
    if (core == NULL) {
        return ENGLISH_COURSE_ERR_NOT_INITIALIZED;
    }
    if (lesson_id == NULL || lesson_id[0] == '\0') {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    const size_t index = english_course_core_find_lesson(core, lesson_id);
    if (index >= core->lesson_count) {
        return ENGLISH_COURSE_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->lesson_count; ++cursor) {
        core->lessons[cursor] = core->lessons[cursor + 1];
    }
    core->lesson_count -= 1;
    memset(
        &core->lessons[core->lesson_count],
        0,
        sizeof(english_course_lesson_t)
    );
    return ENGLISH_COURSE_OK;
}

english_course_error_t english_course_core_record_attempt(
    english_course_core_t *core,
    const english_course_attempt_t *attempt
) {
    if (core == NULL) {
        return ENGLISH_COURSE_ERR_NOT_INITIALIZED;
    }
    if (attempt == NULL || attempt->lesson_id[0] == '\0' ||
        attempt->item_id[0] == '\0' ||
        !english_course_item_type_is_valid(attempt->item_type)) {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    english_course_attempt_t normalized = *attempt;
    if (normalized.score < 0) {
        normalized.score = 0;
    } else if (normalized.score > 100) {
        normalized.score = 100;
    }
    // Passed is derived so a caller cannot claim a pass it did not earn.
    normalized.passed = normalized.score >= ENGLISH_COURSE_PASSING_SCORE;

    if (core->attempt_count >= ENGLISH_COURSE_ATTEMPT_CAPACITY) {
        // Drop the oldest attempt to make room for the newest.
        for (size_t index = ENGLISH_COURSE_ATTEMPT_CAPACITY - 1; index > 0;
             --index) {
            core->attempts[index] = core->attempts[index - 1];
        }
        core->attempts[0] = normalized;
        return ENGLISH_COURSE_OK;
    }
    const size_t last = core->attempt_count;
    for (size_t index = last; index > 0; --index) {
        core->attempts[index] = core->attempts[index - 1];
    }
    core->attempts[0] = normalized;
    core->attempt_count += 1;
    return ENGLISH_COURSE_OK;
}

english_course_error_t english_course_core_clear_attempts(
    english_course_core_t *core
) {
    if (core == NULL) {
        return ENGLISH_COURSE_ERR_NOT_INITIALIZED;
    }
    core->attempt_count = 0;
    memset(core->attempts, 0, sizeof(core->attempts));
    // Clearing a child's practice history also drops anything still queued for
    // the platform so a cleared attempt is never reported after the fact.
    core->pending_count = 0;
    memset(core->pending, 0, sizeof(core->pending));
    return ENGLISH_COURSE_OK;
}

// Find the pending attempt for one (lesson id, item id, practiced-at), or
// pending_count when there is none.
static size_t english_course_core_find_pending(
    const english_course_core_t *core,
    const char *lesson_id,
    const char *item_id,
    int64_t practiced_at_epoch_seconds
) {
    if (core == NULL || lesson_id == NULL || item_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->pending_count; ++index) {
        const english_course_attempt_t *entry = &core->pending[index];
        if (entry->practiced_at_epoch_seconds == practiced_at_epoch_seconds &&
            strcmp(entry->lesson_id, lesson_id) == 0 &&
            strcmp(entry->item_id, item_id) == 0) {
            return index;
        }
    }
    return core->pending_count;
}

english_course_error_t english_course_core_queue_pending(
    english_course_core_t *core,
    const english_course_attempt_t *attempt
) {
    if (core == NULL) {
        return ENGLISH_COURSE_ERR_NOT_INITIALIZED;
    }
    if (attempt == NULL || attempt->lesson_id[0] == '\0' ||
        attempt->item_id[0] == '\0') {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    // Normalize the score the same way the log does so the reported row keeps
    // the clamped score and the derived pass flag.
    english_course_attempt_t normalized = *attempt;
    if (normalized.score < 0) {
        normalized.score = 0;
    } else if (normalized.score > 100) {
        normalized.score = 100;
    }
    normalized.passed = normalized.score >= ENGLISH_COURSE_PASSING_SCORE;
    const size_t existing = english_course_core_find_pending(
        core,
        normalized.lesson_id,
        normalized.item_id,
        normalized.practiced_at_epoch_seconds
    );
    if (existing < core->pending_count) {
        // Replace in place so a retried report reports once, with the latest
        // row, and keeps its position in the queue.
        core->pending[existing] = normalized;
        return ENGLISH_COURSE_OK;
    }
    if (core->pending_count >= ENGLISH_COURSE_OUTBOUND_CAPACITY) {
        // Drop the oldest so the newest attempt still fits; the most recent
        // practice is what a guardian still needs to see.
        for (size_t index = 1; index < core->pending_count; ++index) {
            core->pending[index - 1] = core->pending[index];
        }
        core->pending_count -= 1;
    }
    core->pending[core->pending_count] = normalized;
    core->pending_count += 1;
    return ENGLISH_COURSE_OK;
}

size_t english_course_core_list_pending(
    const english_course_core_t *core,
    english_course_attempt_t *attempts_out,
    size_t capacity
) {
    if (core == NULL) {
        return 0;
    }
    const size_t copied = core->pending_count < capacity
        ? core->pending_count
        : capacity;
    if (attempts_out != NULL) {
        for (size_t index = 0; index < copied; ++index) {
            attempts_out[index] = core->pending[index];
        }
    }
    return core->pending_count;
}

english_course_error_t english_course_core_remove_pending(
    english_course_core_t *core,
    const char *lesson_id,
    const char *item_id,
    int64_t practiced_at_epoch_seconds
) {
    if (core == NULL) {
        return ENGLISH_COURSE_ERR_NOT_INITIALIZED;
    }
    if (lesson_id == NULL || lesson_id[0] == '\0' || item_id == NULL ||
        item_id[0] == '\0') {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    const size_t index = english_course_core_find_pending(
        core,
        lesson_id,
        item_id,
        practiced_at_epoch_seconds
    );
    if (index >= core->pending_count) {
        return ENGLISH_COURSE_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->pending_count; ++cursor) {
        core->pending[cursor] = core->pending[cursor + 1];
    }
    core->pending_count -= 1;
    memset(
        &core->pending[core->pending_count],
        0,
        sizeof(english_course_attempt_t)
    );
    return ENGLISH_COURSE_OK;
}

english_course_error_t english_course_core_lesson_progress(
    const english_course_core_t *core,
    const char *lesson_id,
    english_course_item_progress_t *progress_out,
    size_t capacity,
    size_t *count_out
) {
    if (core == NULL) {
        return ENGLISH_COURSE_ERR_NOT_INITIALIZED;
    }
    if (lesson_id == NULL || lesson_id[0] == '\0' || count_out == NULL) {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    if (progress_out == NULL && capacity > 0) {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    // Track which distinct items were seen so the caller's buffer can be
    // smaller than the number of practiced items without overrunning it.
    english_course_item_progress_t seen[ENGLISH_COURSE_ATTEMPT_CAPACITY];
    size_t seen_count = 0;
    for (size_t index = 0; index < core->attempt_count; ++index) {
        const english_course_attempt_t *attempt = &core->attempts[index];
        if (strcmp(attempt->lesson_id, lesson_id) != 0) {
            continue;
        }
        // Find an existing rollup for this item.
        size_t slot = seen_count;
        for (size_t probe = 0; probe < seen_count; ++probe) {
            if (strcmp(seen[probe].item_id, attempt->item_id) == 0 &&
                seen[probe].item_type == attempt->item_type) {
                slot = probe;
                break;
            }
        }
        if (slot == seen_count) {
            english_course_item_progress_t *entry = &seen[seen_count];
            memset(entry, 0, sizeof(*entry));
            strncpy(
                entry->lesson_id,
                attempt->lesson_id,
                ENGLISH_COURSE_ID_SIZE - 1
            );
            strncpy(
                entry->item_id,
                attempt->item_id,
                ENGLISH_COURSE_ITEM_ID_SIZE - 1
            );
            entry->item_type = attempt->item_type;
            entry->best_score = attempt->score;
            entry->attempts = 1;
            entry->passed = attempt->passed;
            seen_count += 1;
            continue;
        }
        english_course_item_progress_t *entry = &seen[slot];
        if (attempt->score > entry->best_score) {
            entry->best_score = attempt->score;
        }
        entry->attempts += 1;
        entry->passed = entry->passed || attempt->passed;
    }
    const size_t copied = seen_count < capacity ? seen_count : capacity;
    for (size_t index = 0; index < copied; ++index) {
        progress_out[index] = seen[index];
    }
    *count_out = seen_count;
    return ENGLISH_COURSE_OK;
}
