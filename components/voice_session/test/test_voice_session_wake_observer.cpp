#include "voice_session_wake_observer.h"

#include <cassert>
#include <cstdint>
#include <cstring>

static voice_wake_snapshot_t make_snapshot(
    uint32_t sequence,
    uint32_t wake_word_id = 7,
    const char *name = "ni_hao_xiao_zhi",
    uint32_t confidence_milli = 900
) {
    voice_wake_snapshot_t snapshot = {};
    snapshot.observation_sequence = sequence;
    snapshot.latest_event.wake_word_id = wake_word_id;
    snapshot.latest_event.confidence_milli = confidence_milli;
    if (name != nullptr) {
        std::strncpy(
            snapshot.latest_event.wake_word_name,
            name,
            sizeof(snapshot.latest_event.wake_word_name) - 1
        );
    }
    return snapshot;
}

static void test_first_snapshot_sets_baseline_without_a_wake(void) {
    voice_session_wake_observer_t observer = {};
    const voice_wake_snapshot_t snapshot = make_snapshot(0, 0, nullptr);
    const auto result =
        voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NONE);
    assert(observer.initialized);
    assert(observer.observation_sequence == 0);
    voice_wake_event_t event = {};
    assert(!voice_session_wake_observer_peek(&observer, &event));
}

static void test_second_snapshot_after_baseline_is_new(void) {
    voice_session_wake_observer_t observer = {};
    voice_wake_snapshot_t snapshot = make_snapshot(0, 0, nullptr);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);
    snapshot = make_snapshot(1);
    const auto result =
        voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NEW);
    assert(result.new_detections == 1);
}

static void test_uninitialized_snapshot_with_new_event_is_actionable(void) {
    voice_session_wake_observer_t observer = {};
    const voice_wake_snapshot_t snapshot = make_snapshot(1);
    const auto result =
        voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NEW);
    assert(result.new_detections == 1);
    voice_wake_event_t event = {};
    assert(voice_session_wake_observer_peek(&observer, &event));
}

static void test_new_sequence_starts_one_wake_with_real_word(void) {
    voice_session_wake_observer_t observer = {};
    voice_wake_snapshot_t snapshot = make_snapshot(1);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);

    snapshot = make_snapshot(2);
    const auto result =
        voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NEW);
    assert(result.new_detections == 1);
    assert(std::strcmp(result.event.wake_word_name, "ni_hao_xiao_zhi") == 0);

    voice_wake_event_t event = {};
    assert(voice_session_wake_observer_peek(&observer, &event));
    assert(event.wake_word_id == 7);
    event = {};
    assert(voice_session_wake_observer_take(&observer, &event));
    assert(event.wake_word_id == 7);
    assert(!voice_session_wake_observer_take(&observer, &event));
    voice_session_wake_observer_clear_pending(&observer);
    assert(!voice_session_wake_observer_peek(&observer, &event));
}

static void test_duplicate_snapshot_is_stale(void) {
    voice_session_wake_observer_t observer = {};
    voice_session_wake_observer_reset(&observer, UINT32_MAX);
    voice_wake_snapshot_t snapshot = make_snapshot(2);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);
    const auto result =
        voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NONE);
}

static void test_multiple_missed_detections_are_coalesced_to_latest(void) {
    voice_session_wake_observer_t observer = {};
    voice_wake_snapshot_t snapshot = make_snapshot(1);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);
    snapshot = make_snapshot(5, 9, "latest_wake");
    const auto result =
        voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NEW);
    assert(result.new_detections == 4);
    assert(result.event.wake_word_id == 9);
    assert(std::strcmp(result.event.wake_word_name, "latest_wake") == 0);
}

static void test_u32_wrap_is_one_new_wake_and_zero_is_skipped(void) {
    voice_session_wake_observer_t observer = {};
    voice_wake_snapshot_t snapshot = make_snapshot(UINT32_MAX - 1);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);
    snapshot = make_snapshot(UINT32_MAX);
    auto result = voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NEW);
    assert(result.new_detections == 1);

    snapshot = make_snapshot(1);
    result = voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NEW);
    assert(result.new_detections == 1);
    assert(voice_session_wake_observer_advance(UINT32_MAX) == 1);
}

static void test_invalid_event_fails_closed_and_clears_pending(void) {
    voice_session_wake_observer_t observer = {};
    voice_wake_snapshot_t snapshot = make_snapshot(1);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);
    snapshot = make_snapshot(2);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);
    assert(voice_session_wake_observer_peek(&observer, nullptr) == false);
    voice_wake_event_t event = {};
    assert(voice_session_wake_observer_peek(&observer, &event));

    snapshot = make_snapshot(3, 0, "");
    const auto result =
        voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_INVALID);
    assert(!voice_session_wake_observer_peek(&observer, &event));
}

static void test_reset_does_not_replay_old_pending_event(void) {
    voice_session_wake_observer_t observer = {};
    voice_wake_snapshot_t snapshot = make_snapshot(2);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);
    snapshot = make_snapshot(3);
    (void)voice_session_wake_observer_observe(&observer, &snapshot);
    voice_wake_event_t event = {};
    assert(voice_session_wake_observer_peek(&observer, &event));

    snapshot = make_snapshot(0, 0, "");
    const auto result =
        voice_session_wake_observer_observe(&observer, &snapshot);
    assert(result.result == VOICE_SESSION_WAKE_OBSERVATION_NONE);
    assert(!voice_session_wake_observer_peek(&observer, &event));
    voice_session_wake_observer_reset(&observer, 0);
    assert(observer.initialized);
    assert(observer.observation_sequence == 0);
}

int main() {
    test_first_snapshot_sets_baseline_without_a_wake();
    test_second_snapshot_after_baseline_is_new();
    test_uninitialized_snapshot_with_new_event_is_actionable();
    test_new_sequence_starts_one_wake_with_real_word();
    test_duplicate_snapshot_is_stale();
    test_multiple_missed_detections_are_coalesced_to_latest();
    test_u32_wrap_is_one_new_wake_and_zero_is_skipped();
    test_invalid_event_fails_closed_and_clears_pending();
    test_reset_does_not_replay_old_pending_event();
    return 0;
}
