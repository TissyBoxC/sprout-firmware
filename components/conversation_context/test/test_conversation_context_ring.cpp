// Host test for the pure conversation context ring.
//
// It has no ESP-IDF dependency and is built by tools/run_host_tests.ps1
// together with src/conversation_context_ring.c so CI runs it without hardware.

#include "conversation_context_ring.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static void test_append_and_order(void) {
    conversation_context_ring_t ring;
    conversation_context_ring_reset(&ring);
    assert(ring.count == 0);

    for (int index = 0; index < CONVERSATION_CONTEXT_CAPACITY; ++index) {
        char summary[16] = {0};
        std::snprintf(summary, sizeof(summary), "turn_%d", index);
        assert(conversation_context_ring_append(
            &ring,
            "user",
            summary,
            (uint64_t)index
        ));
    }
    assert(ring.count == CONVERSATION_CONTEXT_CAPACITY);

    conversation_context_turn_t turns[CONVERSATION_CONTEXT_CAPACITY];
    const size_t written = conversation_context_ring_recent(
        &ring,
        turns,
        CONVERSATION_CONTEXT_CAPACITY
    );
    assert(written == CONVERSATION_CONTEXT_CAPACITY);
    assert(std::strcmp(turns[0].summary, "turn_0") == 0);
    assert(std::strcmp(turns[written - 1].summary, "turn_7") == 0);
    assert(turns[0].sequence == 0);
    assert(turns[written - 1].sequence == 7);
}

static void test_eviction_keeps_newest(void) {
    conversation_context_ring_t ring;
    conversation_context_ring_reset(&ring);

    for (int index = 0; index < CONVERSATION_CONTEXT_CAPACITY + 3; ++index) {
        char summary[16] = {0};
        std::snprintf(summary, sizeof(summary), "turn_%d", index);
        assert(conversation_context_ring_append(
            &ring,
            "assistant",
            summary,
            0
        ));
    }
    assert(ring.evicted == 3);
    assert(ring.count == CONVERSATION_CONTEXT_CAPACITY);

    conversation_context_turn_t turns[CONVERSATION_CONTEXT_CAPACITY];
    const size_t written = conversation_context_ring_recent(
        &ring,
        turns,
        CONVERSATION_CONTEXT_CAPACITY
    );
    assert(written == CONVERSATION_CONTEXT_CAPACITY);
    assert(std::strcmp(turns[0].summary, "turn_3") == 0);
    assert(std::strcmp(turns[written - 1].summary, "turn_10") == 0);
}

static void test_recent_subset_returns_newest(void) {
    conversation_context_ring_t ring;
    conversation_context_ring_reset(&ring);
    for (int index = 0; index < 5; ++index) {
        char summary[16] = {0};
        std::snprintf(summary, sizeof(summary), "turn_%d", index);
        assert(conversation_context_ring_append(&ring, "user", summary, 0));
    }

    conversation_context_turn_t turns[3];
    const size_t written = conversation_context_ring_recent(&ring, turns, 3);
    assert(written == 3);
    assert(std::strcmp(turns[0].summary, "turn_2") == 0);
    assert(std::strcmp(turns[2].summary, "turn_4") == 0);
}

static void test_summary_truncation_and_empty_rejection(void) {
    conversation_context_ring_t ring;
    conversation_context_ring_reset(&ring);

    assert(!conversation_context_ring_append(&ring, "user", "", 0));
    assert(!conversation_context_ring_append(&ring, "user", nullptr, 0));

    char long_summary[CONVERSATION_CONTEXT_SUMMARY_SIZE + 32];
    std::memset(long_summary, 'a', sizeof(long_summary) - 1);
    long_summary[sizeof(long_summary) - 1] = '\0';
    assert(conversation_context_ring_append(&ring, nullptr, long_summary, 0));
    assert(
        std::strlen(ring.turns[ring.head].summary) ==
        CONVERSATION_CONTEXT_SUMMARY_SIZE
    );
    // A blank role falls back to the user role instead of an empty string.
    assert(std::strcmp(ring.turns[ring.head].role, "user") == 0);
}

int main(void) {
    test_append_and_order();
    test_eviction_keeps_newest();
    test_recent_subset_returns_newest();
    test_summary_truncation_and_empty_rejection();
    return 0;
}
