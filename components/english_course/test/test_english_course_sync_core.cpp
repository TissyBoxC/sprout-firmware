#include "english_course_sync_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static english_course_lesson_t make_lesson(
    const char *lesson_id,
    const char *title,
    int word_count
) {
    english_course_lesson_t lesson = {};
    std::strncpy(lesson.lesson_id, lesson_id, ENGLISH_COURSE_ID_SIZE - 1);
    std::strncpy(lesson.title, title, ENGLISH_COURSE_TEXT_SIZE - 1);
    std::strncpy(lesson.age_tier, "age_3_4", ENGLISH_COURSE_TEXT_SIZE - 1);
    for (int index = 0; index < word_count; ++index) {
        char item[ENGLISH_COURSE_ITEM_ID_SIZE];
        std::snprintf(item, sizeof(item), "word-%d", index);
        std::strncpy(
            lesson.words[index].item_id,
            item,
            ENGLISH_COURSE_ITEM_ID_SIZE - 1
        );
        std::strncpy(
            lesson.words[index].term,
            "apple",
            ENGLISH_COURSE_TEXT_SIZE - 1
        );
    }
    lesson.word_count = static_cast<size_t>(word_count);
    return lesson;
}

int main() {
    english_course_core_t core = {};
    english_course_core_init(&core);
    const english_course_lesson_t local = make_lesson(
        "lesson_local",
        "本地课程",
        1
    );
    assert(english_course_core_upsert_lesson(&core, &local) == ENGLISH_COURSE_OK);

    // Seed one practice attempt so we can prove a catalog refresh keeps it.
    english_course_attempt_t attempt = {};
    std::strncpy(attempt.lesson_id, "lesson_local", ENGLISH_COURSE_ID_SIZE - 1);
    std::strncpy(attempt.item_id, "word-0", ENGLISH_COURSE_ITEM_ID_SIZE - 1);
    attempt.item_type = ENGLISH_ITEM_WORD;
    attempt.score = 88;
    assert(english_course_core_record_attempt(&core, &attempt) == ENGLISH_COURSE_OK);

    english_course_lesson_t remote[] = {
        make_lesson("lesson_fruit", "水果", 2),
        make_lesson("lesson_color", "颜色", 1),
        make_lesson("lesson_fruit", "水果重复", 1),
    };
    assert(
        english_course_sync_core_replace_lessons(&core, remote, 3) ==
        ENGLISH_COURSE_OK
    );
    assert(core.lesson_count == 2);
    assert(std::strcmp(core.lessons[0].lesson_id, "lesson_fruit") == 0);
    assert(std::strcmp(core.lessons[1].lesson_id, "lesson_color") == 0);
    // The practice log is untouched by a catalog refresh.
    assert(core.attempt_count == 1);
    assert(std::strcmp(core.attempts[0].lesson_id, "lesson_local") == 0);

    // An empty valid remote catalog clears the lessons but keeps attempts.
    assert(
        english_course_sync_core_replace_lessons(&core, nullptr, 0) ==
        ENGLISH_COURSE_OK
    );
    assert(core.lesson_count == 0);
    assert(core.attempt_count == 1);

    // A snapshot with only invalid lessons is rejected without touching state.
    const english_course_lesson_t valid = make_lesson("lesson_fruit", "水果", 1);
    assert(english_course_core_upsert_lesson(&core, &valid) == ENGLISH_COURSE_OK);
    english_course_lesson_t invalid_only[] = {
        make_lesson("", "无编号", 1),
        make_lesson("lesson_empty", "无内容", 0),
    };
    assert(
        english_course_sync_core_replace_lessons(&core, invalid_only, 2) ==
        ENGLISH_COURSE_ERR_INVALID_ARGUMENT
    );
    assert(core.lesson_count == 1);
    assert(std::strcmp(core.lessons[0].lesson_id, "lesson_fruit") == 0);

    // Capacity is bounded without rejecting the whole snapshot.
    english_course_lesson_t too_many[ENGLISH_COURSE_LESSON_CAPACITY + 2] = {};
    char ids[ENGLISH_COURSE_LESSON_CAPACITY + 2][ENGLISH_COURSE_ID_SIZE] = {};
    for (int index = 0; index < ENGLISH_COURSE_LESSON_CAPACITY + 2; ++index) {
        std::snprintf(ids[index], sizeof(ids[index]), "lesson_%03d", index);
        too_many[index] = make_lesson(ids[index], "课程", 1);
    }
    assert(
        english_course_sync_core_replace_lessons(
            &core,
            too_many,
            ENGLISH_COURSE_LESSON_CAPACITY + 2
        ) == ENGLISH_COURSE_OK
    );
    assert(core.lesson_count == ENGLISH_COURSE_LESSON_CAPACITY);
    assert(std::strcmp(core.lessons[0].lesson_id, "lesson_000") == 0);

    return 0;
}
