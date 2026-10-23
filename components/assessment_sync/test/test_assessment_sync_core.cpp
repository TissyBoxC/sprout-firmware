#include <cassert>
#include <cstdio>
#include <cstring>

#include "assessment_sync_core.h"

namespace {

assessment_sync_session_t parse_full_session() {
    assessment_sync_session_t session = {};
    assert(assessment_sync_parse_session(
        &session,
        "session-1",
        "active",
        true,
        2,
        3,
        2,
        67,
        true,
        3,
        "item-0004",
        "word",
        "apple",
        3
    ));
    return session;
}

void test_item_type_vocabulary() {
    assert(assessment_sync_item_type_is_valid("word"));
    assert(assessment_sync_item_type_is_valid("sentence"));
    // The platform rejects anything else with 422, so the device must too
    // instead of queueing an answer that can never be accepted.
    assert(!assessment_sync_item_type_is_valid("phrase"));
    assert(!assessment_sync_item_type_is_valid(""));
    assert(!assessment_sync_item_type_is_valid(nullptr));
}

void test_parse_keeps_the_current_item() {
    const assessment_sync_session_t session = parse_full_session();
    assert(std::strcmp(session.session_id, "session-1") == 0);
    assert(std::strcmp(session.state, "active") == 0);
    assert(session.accepts_answers);
    assert(session.difficulty == 2);
    assert(session.answered_count == 3);
    assert(session.correct_count == 2);
    assert(session.mastery == 67);
    assert(session.item.has_item);
    assert(session.item.position == 3);
    assert(session.item.difficulty == 3);
    assert(std::strcmp(session.item.item_id, "item-0004") == 0);
    assert(std::strcmp(session.item.item_type, "word") == 0);
    assert(std::strcmp(session.item.text, "apple") == 0);
}

void test_parse_accepts_a_finished_session_without_item() {
    assessment_sync_session_t session = {};
    assert(assessment_sync_parse_session(
        &session,
        "session-1",
        "completed",
        false,
        4,
        10,
        7,
        70,
        false,
        0,
        "",
        "",
        "",
        0
    ));
    assert(!session.accepts_answers);
    // A finished run has no item to present, and the device must not invent one.
    assert(!session.item.has_item);
    assert(std::strcmp(session.item.text, "") == 0);
}

void test_parse_rejects_unusable_payloads() {
    assessment_sync_session_t session = {};
    // Unknown state: the device cannot know whether answers are accepted.
    assert(!assessment_sync_parse_session(
        &session, "s", "paused", true, 2, 0, 0, 0, false, 0, "", "", "", 0
    ));
    // Missing session id.
    assert(!assessment_sync_parse_session(
        &session, "", "active", true, 2, 0, 0, 0, false, 0, "", "", "", 0
    ));
    // Difficulty outside the platform range.
    assert(!assessment_sync_parse_session(
        &session, "s", "active", true, 6, 0, 0, 0, false, 0, "", "", "", 0
    ));
    assert(!assessment_sync_parse_session(
        &session, "s", "active", true, 0, 0, 0, 0, false, 0, "", "", "", 0
    ));
    // An item without target text must never reach the screen.
    assert(!assessment_sync_parse_session(
        &session, "s", "active", true, 2, 0, 0, 0, true, 0, "i", "word", "", 2
    ));
    // An item with an unknown type cannot be answered.
    assert(!assessment_sync_parse_session(
        &session, "s", "active", true, 2, 0, 0, 0, true, 0, "i", "phrase", "a", 2
    ));
    // Position beyond the ten-item run.
    assert(!assessment_sync_parse_session(
        &session, "s", "active", true, 2, 0, 0, 0, true, 10, "i", "word", "a", 2
    ));
    // Mastery outside 0-100.
    assert(!assessment_sync_parse_session(
        &session, "s", "active", true, 2, 0, 0, 101, false, 0, "", "", "", 0
    ));
    assert(!assessment_sync_parse_session(
        nullptr, "s", "active", true, 2, 0, 0, 0, false, 0, "", "", "", 0
    ));
}

void test_answer_requires_a_guardian_score_in_range() {
    assessment_sync_answer_t answer = {};
    assert(assessment_sync_answer_from_values(
        &answer, "item-1", "word", "apple", 80
    ));
    assert(answer.score == 80);
    // Out-of-range scores are refused rather than clamped: a clamped value
    // would report a performance the guardian never gave.
    assert(!assessment_sync_answer_from_values(
        &answer, "item-1", "word", "apple", 101
    ));
    assert(!assessment_sync_answer_from_values(
        &answer, "item-1", "word", "apple", -1
    ));
    assert(assessment_sync_answer_from_values(
        &answer, "item-1", "word", "apple", 0
    ));
    assert(assessment_sync_answer_from_values(
        &answer, "item-1", "word", "apple", 100
    ));
    assert(!assessment_sync_answer_from_values(
        &answer, "", "word", "apple", 50
    ));
    assert(!assessment_sync_answer_from_values(
        &answer, "item-1", "word", "", 50
    ));
    assert(!assessment_sync_answer_from_values(
        &answer, "item-1", "letter", "apple", 50
    ));
}

void test_answer_queue_rejects_duplicates_and_stays_bounded() {
    assessment_sync_answer_t answers[2] = {};
    size_t count = 0;
    assessment_sync_answer_t answer = {};
    assert(assessment_sync_answer_from_values(
        &answer, "item-1", "word", "apple", 70
    ));
    assert(assessment_sync_answer_push(answers, 2, &count, &answer));
    // The same item twice would occupy two answer positions for one answer.
    assert(!assessment_sync_answer_push(answers, 2, &count, &answer));
    assert(count == 1);
    assert(assessment_sync_answer_find(answers, count, "item-1"));
    assert(!assessment_sync_answer_find(answers, count, "item-2"));

    assert(assessment_sync_answer_from_values(
        &answer, "item-2", "word", "pear", 40
    ));
    assert(assessment_sync_answer_push(answers, 2, &count, &answer));
    assessment_sync_answer_t third = {};
    assert(assessment_sync_answer_from_values(
        &third, "item-3", "word", "plum", 90
    ));
    assert(!assessment_sync_answer_push(answers, 2, &count, &third));
    assert(count == 2);
}

void test_answer_pop_keeps_report_order() {
    assessment_sync_answer_t answers[3] = {};
    size_t count = 0;
    const char *const items[] = {"item-1", "item-2", "item-3"};
    for (const char *item : items) {
        assessment_sync_answer_t answer = {};
        assert(assessment_sync_answer_from_values(
            &answer, item, "word", "target", 50
        ));
        assert(assessment_sync_answer_push(answers, 3, &count, &answer));
    }
    assessment_sync_answer_pop(answers, &count);
    assert(count == 2);
    assert(std::strcmp(answers[0].item_id, "item-2") == 0);
    assert(std::strcmp(answers[1].item_id, "item-3") == 0);
    assessment_sync_answer_pop(answers, &count);
    assessment_sync_answer_pop(answers, &count);
    assert(count == 0);
    // Popping an empty queue must not underflow the count.
    assessment_sync_answer_pop(answers, &count);
    assert(count == 0);
}

void test_answer_must_match_the_presented_item() {
    assessment_sync_item_t item = {};
    item.has_item = true;
    item.position = 2;
    std::strncpy(item.item_id, "item-3", sizeof(item.item_id) - 1);
    std::strncpy(item.item_type, "word", sizeof(item.item_type) - 1);
    std::strncpy(item.text, "banana", sizeof(item.text) - 1);
    assert(assessment_sync_answer_matches_item(
        &item, "item-3", "word", "banana"
    ));
    assert(!assessment_sync_answer_matches_item(
        &item, "item-4", "word", "banana"
    ));
    assert(!assessment_sync_answer_matches_item(
        &item, "item-3", "sentence", "banana"
    ));
    // The target text is validated against the course item by the platform, so
    // a mismatched text would be refused with 422.
    assert(!assessment_sync_answer_matches_item(
        &item, "item-3", "word", "apple"
    ));
    item.has_item = false;
    assert(!assessment_sync_answer_matches_item(
        &item, "item-3", "word", "banana"
    ));
}

void test_backoff_doubles_then_caps() {
    const uint32_t base = 15;
    const uint32_t maximum = 300;
    assert(assessment_sync_backoff_seconds(base, maximum, 0) == 15);
    assert(assessment_sync_backoff_seconds(base, maximum, 1) == 30);
    assert(assessment_sync_backoff_seconds(base, maximum, 2) == 60);
    assert(assessment_sync_backoff_seconds(base, maximum, 3) == 120);
    assert(assessment_sync_backoff_seconds(base, maximum, 4) == 240);
    assert(assessment_sync_backoff_seconds(base, maximum, 5) == 300);
    // A long outage must not wrap the doubling back to a short delay.
    assert(assessment_sync_backoff_seconds(base, maximum, 64) == 300);
    assert(assessment_sync_backoff_seconds(base, maximum, 4000) == 300);
    assert(assessment_sync_backoff_seconds(0, maximum, 3) == maximum);
}

}  // namespace

int main() {
    test_item_type_vocabulary();
    test_parse_keeps_the_current_item();
    test_parse_accepts_a_finished_session_without_item();
    test_parse_rejects_unusable_payloads();
    test_answer_requires_a_guardian_score_in_range();
    test_answer_queue_rejects_duplicates_and_stays_bounded();
    test_answer_pop_keeps_report_order();
    test_answer_must_match_the_presented_item();
    test_backoff_doubles_then_caps();
    std::printf("assessment_sync_core: all checks passed\n");
    return 0;
}
