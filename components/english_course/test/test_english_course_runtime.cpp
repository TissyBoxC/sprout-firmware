#include "english_course.h"

#include <cassert>
#include <cstring>

static english_course_lesson_t make_lesson(
    const char *lesson_id,
    const char *title,
    const char *term
) {
    english_course_lesson_t lesson;
    std::memset(&lesson, 0, sizeof(lesson));
    std::strncpy(lesson.lesson_id, lesson_id, ENGLISH_COURSE_ID_SIZE - 1);
    std::strncpy(lesson.title, title, ENGLISH_COURSE_TEXT_SIZE - 1);
    std::strncpy(lesson.age_tier, "age_3_4", ENGLISH_COURSE_TEXT_SIZE - 1);
    std::strncpy(
        lesson.words[0].item_id,
        "word-apple",
        ENGLISH_COURSE_ITEM_ID_SIZE - 1
    );
    std::strncpy(
        lesson.words[0].term,
        term,
        ENGLISH_COURSE_TEXT_SIZE - 1
    );
    std::strncpy(
        lesson.words[0].translation,
        "苹果",
        ENGLISH_COURSE_TEXT_SIZE - 1
    );
    lesson.word_count = 1;
    return lesson;
}

static english_course_attempt_t make_attempt(
    const char *lesson_id,
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
        "word-apple",
        ENGLISH_COURSE_ITEM_ID_SIZE - 1
    );
    attempt.item_type = ENGLISH_ITEM_WORD;
    attempt.score = score;
    attempt.practiced_at_epoch_seconds = 1700000000;
    return attempt;
}

int main() {
    assert(english_course_init() == ESP_OK);
    assert(english_course_is_ready());
    assert(english_course_lesson_count() == 0);
    assert(english_course_attempt_count() == 0);

    const english_course_lesson_t fruit = make_lesson(
        "lesson_fruit",
        "水果",
        "apple"
    );
    assert(english_course_upsert_lesson(&fruit) == ESP_OK);
    assert(english_course_lesson_count() == 1);

    english_course_lesson_t recalled;
    std::memset(&recalled, 0, sizeof(recalled));
    assert(english_course_get_lesson("lesson_fruit", &recalled));
    assert(std::strcmp(recalled.title, "水果") == 0);
    assert(recalled.word_count == 1);
    assert(std::strcmp(recalled.words[0].term, "apple") == 0);
    assert(!english_course_get_lesson("lesson_missing", &recalled));

    english_course_lesson_t list[ENGLISH_COURSE_LESSON_CAPACITY];
    size_t listed = 99;
    assert(
        english_course_list_lessons(
            list,
            ENGLISH_COURSE_LESSON_CAPACITY,
            &listed
        ) == ESP_OK
    );
    assert(listed == 1);
    assert(std::strcmp(list[0].lesson_id, "lesson_fruit") == 0);

    // A recording attempt persists and derives passed from the clamped score.
    const english_course_attempt_t pass = make_attempt("lesson_fruit", 82);
    assert(english_course_record_attempt(&pass) == ESP_OK);
    assert(english_course_attempt_count() == 1);

    const english_course_attempt_t low = make_attempt("lesson_fruit", 40);
    assert(english_course_record_attempt(&low) == ESP_OK);
    assert(english_course_attempt_count() == 2);

    english_course_attempt_t attempts[ENGLISH_COURSE_ATTEMPT_CAPACITY];
    size_t attempt_count = 0;
    assert(
        english_course_list_attempts(
            attempts,
            ENGLISH_COURSE_ATTEMPT_CAPACITY,
            &attempt_count
        ) == ESP_OK
    );
    assert(attempt_count == 2);
    // Newest first.
    assert(attempts[0].score == 40);
    assert(!attempts[0].passed);
    assert(attempts[1].passed);

    english_course_item_progress_t progress[8];
    size_t progress_count = 0;
    assert(
        english_course_lesson_progress(
            "lesson_fruit",
            progress,
            8,
            &progress_count
        ) == ESP_OK
    );
    assert(progress_count == 1);
    assert(progress[0].best_score == 82);
    assert(progress[0].attempts == 2);
    assert(progress[0].passed);

    // The blob survives a re-init, standing in for a device restart.
    assert(english_course_init() == ESP_OK);
    assert(english_course_lesson_count() == 1);
    assert(english_course_attempt_count() == 2);
    assert(english_course_get_lesson("lesson_fruit", &recalled));

    // Clearing attempts persists across a restart too.
    assert(english_course_clear_attempts() == ESP_OK);
    assert(english_course_attempt_count() == 0);
    assert(english_course_init() == ESP_OK);
    assert(english_course_attempt_count() == 0);
    assert(english_course_lesson_count() == 1);

    // Removing the lesson persists.
    assert(english_course_remove_lesson("lesson_fruit") == ESP_OK);
    assert(english_course_lesson_count() == 0);
    assert(english_course_init() == ESP_OK);
    assert(english_course_lesson_count() == 0);

    assert(
        std::strcmp(english_course_item_type_name(ENGLISH_ITEM_WORD), "word") ==
        0
    );
    english_course_item_type_t parsed;
    assert(english_course_item_type_from_name("sentence", &parsed));
    assert(parsed == ENGLISH_ITEM_SENTENCE);
    assert(!english_course_item_type_from_name("phonics", &parsed));

    return 0;
}
