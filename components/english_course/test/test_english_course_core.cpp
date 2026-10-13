#include "english_course_core.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static english_course_lesson_t make_lesson(
    const char *lesson_id,
    const char *title,
    const char *age_tier
) {
    english_course_lesson_t lesson;
    std::memset(&lesson, 0, sizeof(lesson));
    std::strncpy(lesson.lesson_id, lesson_id, ENGLISH_COURSE_ID_SIZE - 1);
    std::strncpy(lesson.title, title, ENGLISH_COURSE_TEXT_SIZE - 1);
    std::strncpy(lesson.age_tier, age_tier, ENGLISH_COURSE_TEXT_SIZE - 1);
    return lesson;
}

static void add_word(
    english_course_lesson_t *lesson,
    const char *item_id,
    const char *term,
    const char *translation
) {
    english_course_word_t *word = &lesson->words[lesson->word_count];
    std::memset(word, 0, sizeof(*word));
    std::strncpy(word->item_id, item_id, ENGLISH_COURSE_ITEM_ID_SIZE - 1);
    std::strncpy(word->term, term, ENGLISH_COURSE_TEXT_SIZE - 1);
    std::strncpy(
        word->translation,
        translation,
        ENGLISH_COURSE_TEXT_SIZE - 1
    );
    lesson->word_count += 1;
}

static void add_sentence(
    english_course_lesson_t *lesson,
    const char *item_id,
    const char *pattern
) {
    english_course_sentence_t *sentence =
        &lesson->sentences[lesson->sentence_count];
    std::memset(sentence, 0, sizeof(*sentence));
    std::strncpy(
        sentence->item_id,
        item_id,
        ENGLISH_COURSE_ITEM_ID_SIZE - 1
    );
    std::strncpy(
        sentence->pattern,
        pattern,
        ENGLISH_COURSE_LONG_TEXT_SIZE - 1
    );
    lesson->sentence_count += 1;
}

static english_course_attempt_t make_attempt(
    const char *lesson_id,
    const char *item_id,
    english_course_item_type_t item_type,
    int score
) {
    english_course_attempt_t attempt;
    std::memset(&attempt, 0, sizeof(attempt));
    std::strncpy(
        attempt.lesson_id,
        lesson_id,
        ENGLISH_COURSE_ID_SIZE - 1
    );
    std::strncpy(
        attempt.item_id,
        item_id,
        ENGLISH_COURSE_ITEM_ID_SIZE - 1
    );
    attempt.item_type = item_type;
    std::strncpy(
        attempt.target_text,
        "apple",
        ENGLISH_COURSE_LONG_TEXT_SIZE - 1
    );
    std::strncpy(
        attempt.recognized_text,
        "apple",
        ENGLISH_COURSE_RECOGNIZED_TEXT_SIZE - 1
    );
    attempt.score = score;
    attempt.practiced_at_epoch_seconds = 1700000000;
    return attempt;
}

static english_course_attempt_t make_attempt_at(
    const char *lesson_id,
    const char *item_id,
    int score,
    int64_t practiced_at
) {
    english_course_attempt_t attempt = make_attempt(
        lesson_id,
        item_id,
        ENGLISH_ITEM_WORD,
        score
    );
    attempt.practiced_at_epoch_seconds = practiced_at;
    return attempt;
}

int main() {
    english_course_core_t core;
    english_course_core_init(&core);
    assert(core.lesson_count == 0);
    assert(core.attempt_count == 0);

    english_course_lesson_t fruit = make_lesson(
        "lesson_fruit",
        "水果",
        "age_3_4"
    );
    add_word(&fruit, "word-apple", "apple", "苹果");
    add_word(&fruit, "word-banana", "banana", "香蕉");
    add_sentence(&fruit, "sent-like", "I like ___.");
    assert(
        english_course_core_upsert_lesson(&core, &fruit) == ENGLISH_COURSE_OK
    );
    assert(core.lesson_count == 1);
    assert(
        std::strcmp(core.lessons[0].lesson_id, "lesson_fruit") == 0
    );
    assert(core.lessons[0].word_count == 2);
    assert(core.lessons[0].sentence_count == 1);

    english_course_lesson_t school = make_lesson(
        "lesson_school",
        "学校",
        "age_5_6"
    );
    add_word(&school, "word-book", "book", "书");
    assert(
        english_course_core_upsert_lesson(&core, &school) == ENGLISH_COURSE_OK
    );
    assert(core.lesson_count == 2);
    // Newest change leads.
    assert(
        std::strcmp(core.lessons[0].lesson_id, "lesson_school") == 0
    );

    // Re-upserting an existing lesson replaces it and moves it to the head.
    english_course_lesson_t fruit_v2 = make_lesson(
        "lesson_fruit",
        "水果（更新）",
        "age_3_4"
    );
    add_word(&fruit_v2, "word-apple", "apple", "苹果");
    assert(
        english_course_core_upsert_lesson(&core, &fruit_v2) ==
        ENGLISH_COURSE_OK
    );
    assert(core.lesson_count == 2);
    assert(
        std::strcmp(core.lessons[0].lesson_id, "lesson_fruit") == 0
    );

    // Find reports the index, or the count when absent.
    assert(english_course_core_find_lesson(&core, "lesson_fruit") == 0);
    assert(
        english_course_core_find_lesson(&core, "lesson_missing") ==
        core.lesson_count
    );

    // Invalid lessons are rejected.
    english_course_lesson_t invalid = make_lesson("", "空", "age_3_4");
    assert(
        english_course_core_upsert_lesson(&core, &invalid) ==
        ENGLISH_COURSE_ERR_INVALID_ARGUMENT
    );
    invalid = make_lesson("lesson_bad", "坏", "age_3_4");
    invalid.word_count = ENGLISH_COURSE_WORDS_PER_LESSON + 1;
    assert(
        english_course_core_upsert_lesson(&core, &invalid) ==
        ENGLISH_COURSE_ERR_INVALID_ARGUMENT
    );

    // Removing a missing lesson is success.
    assert(
        english_course_core_remove_lesson(&core, "lesson_missing") ==
        ENGLISH_COURSE_OK
    );
    assert(
        english_course_core_remove_lesson(&core, "lesson_school") ==
        ENGLISH_COURSE_OK
    );
    assert(core.lesson_count == 1);

    // Recording an attempt clamps the score and derives passed.
    const english_course_attempt_t low = make_attempt(
        "lesson_fruit",
        "word-apple",
        ENGLISH_ITEM_WORD,
        -20
    );
    assert(
        english_course_core_record_attempt(&core, &low) == ENGLISH_COURSE_OK
    );
    assert(core.attempts[0].score == 0);
    assert(!core.attempts[0].passed);

    const english_course_attempt_t high = make_attempt(
        "lesson_fruit",
        "word-apple",
        ENGLISH_ITEM_WORD,
        140
    );
    assert(
        english_course_core_record_attempt(&core, &high) == ENGLISH_COURSE_OK
    );
    assert(core.attempts[0].score == 100);
    assert(core.attempts[0].passed);
    assert(core.attempt_count == 2);

    const english_course_attempt_t pass = make_attempt(
        "lesson_fruit",
        "word-banana",
        ENGLISH_ITEM_WORD,
        72
    );
    assert(
        english_course_core_record_attempt(&core, &pass) == ENGLISH_COURSE_OK
    );
    const english_course_attempt_t sentence = make_attempt(
        "lesson_fruit",
        "sent-like",
        ENGLISH_ITEM_SENTENCE,
        58
    );
    assert(
        english_course_core_record_attempt(&core, &sentence) ==
        ENGLISH_COURSE_OK
    );

    // An invalid attempt is rejected.
    english_course_attempt_t bad = make_attempt(
        "",
        "word-apple",
        ENGLISH_ITEM_WORD,
        80
    );
    assert(
        english_course_core_record_attempt(&core, &bad) ==
        ENGLISH_COURSE_ERR_INVALID_ARGUMENT
    );
    bad = make_attempt(
        "lesson_fruit",
        "word-apple",
        static_cast<english_course_item_type_t>(99),
        80
    );
    assert(
        english_course_core_record_attempt(&core, &bad) ==
        ENGLISH_COURSE_ERR_INVALID_ARGUMENT
    );

    // Progress rolls up best score, attempts, and pass per distinct item.
    english_course_item_progress_t progress[8];
    size_t count = 0;
    assert(
        english_course_core_lesson_progress(
            &core,
            "lesson_fruit",
            progress,
            8,
            &count
        ) == ENGLISH_COURSE_OK
    );
    assert(count == 3);
    // Distinct items lead with the newest attempt's item, so the sentence
    // recorded last is first, then the two words.
    assert(std::strcmp(progress[0].item_id, "sent-like") == 0);
    assert(progress[0].best_score == 58);
    assert(!progress[0].passed);
    // word-banana passes at 72.
    assert(std::strcmp(progress[1].item_id, "word-banana") == 0);
    assert(progress[1].best_score == 72);
    assert(progress[1].passed);
    // word-apple keeps its best score across both attempts.
    assert(std::strcmp(progress[2].item_id, "word-apple") == 0);
    assert(progress[2].best_score == 100);
    assert(progress[2].attempts == 2);
    assert(progress[2].passed);

    // A lesson with no attempts reports zero distinct items.
    assert(
        english_course_core_lesson_progress(
            &core,
            "lesson_school",
            progress,
            8,
            &count
        ) == ENGLISH_COURSE_OK
    );
    assert(count == 0);

    // A caller buffer smaller than the distinct count is not overrun, but the
    // full distinct count is still reported.
    english_course_item_progress_t tiny[1];
    assert(
        english_course_core_lesson_progress(
            &core,
            "lesson_fruit",
            tiny,
            1,
            &count
        ) == ENGLISH_COURSE_OK
    );
    assert(count == 3);
    assert(std::strcmp(tiny[0].item_id, "sent-like") == 0);

    // Clearing attempts resets the log.
    assert(english_course_core_clear_attempts(&core) == ENGLISH_COURSE_OK);
    assert(core.attempt_count == 0);

    // Capacity is bounded: inserting past the lesson limit reports FULL.
    english_course_core_t full;
    english_course_core_init(&full);
    char id[ENGLISH_COURSE_ID_SIZE];
    for (int index = 0; index < ENGLISH_COURSE_LESSON_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "lesson_%03d", index);
        const english_course_lesson_t lesson = make_lesson(
            id,
            "课程",
            "age_3_4"
        );
        assert(
            english_course_core_upsert_lesson(&full, &lesson) ==
            ENGLISH_COURSE_OK
        );
    }
    assert(full.lesson_count == ENGLISH_COURSE_LESSON_CAPACITY);
    const english_course_lesson_t overflow = make_lesson(
        "lesson_overflow",
        "溢出",
        "age_3_4"
    );
    assert(
        english_course_core_upsert_lesson(&full, &overflow) ==
        ENGLISH_COURSE_ERR_FULL
    );

    // The attempt log is bounded: appending past the limit drops the oldest.
    english_course_core_t log;
    english_course_core_init(&log);
    for (int index = 0; index < ENGLISH_COURSE_ATTEMPT_CAPACITY + 5; ++index) {
        const english_course_attempt_t attempt = make_attempt(
            "lesson_fruit",
            "word-apple",
            ENGLISH_ITEM_WORD,
            index % 101
        );
        assert(
            english_course_core_record_attempt(&log, &attempt) ==
            ENGLISH_COURSE_OK
        );
    }
    assert(log.attempt_count == ENGLISH_COURSE_ATTEMPT_CAPACITY);

    // The pending queue dedupes by (lesson, item, practiced-at), keeps oldest
    // first, and drops the oldest when full.
    english_course_core_t queue;
    english_course_core_init(&queue);
    assert(queue.pending_count == 0);
    const english_course_attempt_t first = make_attempt_at(
        "lesson_fruit",
        "word-apple",
        70,
        1700000000
    );
    assert(
        english_course_core_queue_pending(&queue, &first) ==
        ENGLISH_COURSE_OK
    );
    assert(queue.pending_count == 1);
    const english_course_attempt_t second = make_attempt_at(
        "lesson_fruit",
        "word-apple",
        40,
        1700000100
    );
    assert(
        english_course_core_queue_pending(&queue, &second) ==
        ENGLISH_COURSE_OK
    );
    assert(queue.pending_count == 2);
    // A repeat of the same (lesson, item, practiced-at) replaces in place.
    const english_course_attempt_t retry = make_attempt_at(
        "lesson_fruit",
        "word-apple",
        95,
        1700000000
    );
    assert(
        english_course_core_queue_pending(&queue, &retry) ==
        ENGLISH_COURSE_OK
    );
    assert(queue.pending_count == 2);
    assert(queue.pending[0].practiced_at_epoch_seconds == 1700000000);
    assert(queue.pending[0].score == 95);
    assert(queue.pending[0].passed);
    assert(queue.pending[1].practiced_at_epoch_seconds == 1700000100);

    english_course_attempt_t listed[8];
    std::memset(listed, 0, sizeof(listed));
    assert(english_course_core_list_pending(&queue, listed, 8) == 2);
    assert(listed[0].practiced_at_epoch_seconds == 1700000000);
    assert(listed[1].practiced_at_epoch_seconds == 1700000100);

    // Acknowledging a missing entry is success.
    assert(
        english_course_core_remove_pending(
            &queue,
            "lesson_fruit",
            "word-apple",
            1700009999
        ) == ENGLISH_COURSE_OK
    );
    assert(queue.pending_count == 2);
    assert(
        english_course_core_remove_pending(
            &queue,
            "lesson_fruit",
            "word-apple",
            1700000000
        ) == ENGLISH_COURSE_OK
    );
    assert(queue.pending_count == 1);
    assert(queue.pending[0].practiced_at_epoch_seconds == 1700000100);

    // The queue is bounded to the same capacity as the log.
    english_course_core_t bounded_queue;
    english_course_core_init(&bounded_queue);
    for (int index = 0; index < ENGLISH_COURSE_OUTBOUND_CAPACITY + 3; ++index) {
        const english_course_attempt_t attempt = make_attempt_at(
            "lesson_fruit",
            "word-apple",
            index % 101,
            1700000000 + index
        );
        assert(
            english_course_core_queue_pending(&bounded_queue, &attempt) ==
            ENGLISH_COURSE_OK
        );
    }
    assert(bounded_queue.pending_count == ENGLISH_COURSE_OUTBOUND_CAPACITY);
    // Oldest rows are dropped, so the newest persists at the tail.
    assert(
        bounded_queue.pending[bounded_queue.pending_count - 1]
            .practiced_at_epoch_seconds ==
        1700000000 + ENGLISH_COURSE_OUTBOUND_CAPACITY + 2
    );

    // Clearing a child's history also drops anything queued for the platform.
    assert(
        english_course_core_clear_attempts(&bounded_queue) ==
        ENGLISH_COURSE_OK
    );
    assert(bounded_queue.pending_count == 0);

    return 0;
}
