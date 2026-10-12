#include "english_course_sync_core.h"

#include <string.h>

static bool english_course_sync_lesson_is_valid(
    const english_course_lesson_t *lesson
) {
    if (lesson == NULL || lesson->lesson_id[0] == '\0') {
        return false;
    }
    if (lesson->word_count > ENGLISH_COURSE_WORDS_PER_LESSON ||
        lesson->sentence_count > ENGLISH_COURSE_SENTENCES_PER_LESSON) {
        return false;
    }
    if (lesson->word_count == 0 && lesson->sentence_count == 0) {
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

static bool english_course_sync_contains(
    const char seen_ids[][ENGLISH_COURSE_ID_SIZE],
    size_t seen_count,
    const char *lesson_id
) {
    for (size_t index = 0; index < seen_count; ++index) {
        if (strcmp(seen_ids[index], lesson_id) == 0) {
            return true;
        }
    }
    return false;
}

english_course_error_t english_course_sync_core_replace_lessons(
    english_course_core_t *core,
    const english_course_lesson_t *lessons,
    size_t lesson_count
) {
    if (core == NULL) {
        return ENGLISH_COURSE_ERR_NOT_INITIALIZED;
    }
    if (lessons == NULL && lesson_count > 0) {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    if (lesson_count == 0) {
        core->lesson_count = 0;
        memset(core->lessons, 0, sizeof(core->lessons));
        return ENGLISH_COURSE_OK;
    }

    // Track which lessons were accepted so the second pass can copy them into
    // the cache without ever needing a second full-size catalog on the stack.
    char seen_ids[ENGLISH_COURSE_LESSON_CAPACITY][ENGLISH_COURSE_ID_SIZE] = {};
    size_t chosen[ENGLISH_COURSE_LESSON_CAPACITY] = {};
    size_t chosen_count = 0;
    for (size_t index = 0; index < lesson_count; ++index) {
        const english_course_lesson_t *lesson = &lessons[index];
        if (!english_course_sync_lesson_is_valid(lesson) ||
            english_course_sync_contains(
                seen_ids,
                chosen_count,
                lesson->lesson_id
            )) {
            continue;
        }
        if (chosen_count >= ENGLISH_COURSE_LESSON_CAPACITY) {
            break;
        }
        memcpy(
            seen_ids[chosen_count],
            lesson->lesson_id,
            ENGLISH_COURSE_ID_SIZE
        );
        chosen[chosen_count] = index;
        chosen_count += 1;
    }
    if (chosen_count == 0) {
        return ENGLISH_COURSE_ERR_INVALID_ARGUMENT;
    }
    memset(core->lessons, 0, sizeof(core->lessons));
    for (size_t index = 0; index < chosen_count; ++index) {
        core->lessons[index] = lessons[chosen[index]];
    }
    core->lesson_count = chosen_count;
    return ENGLISH_COURSE_OK;
}
