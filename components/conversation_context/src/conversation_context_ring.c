#include "conversation_context_ring.h"

#include <string.h>

void conversation_context_ring_reset(conversation_context_ring_t *ring) {
    if (ring == NULL) {
        return;
    }
    memset(ring, 0, sizeof(*ring));
}

bool conversation_context_ring_append(
    conversation_context_ring_t *ring,
    const char *role,
    const char *summary,
    uint64_t created_at_ms
) {
    if (ring == NULL || summary == NULL || summary[0] == '\0') {
        return false;
    }

    size_t write_index = 0;
    if (ring->count < CONVERSATION_CONTEXT_CAPACITY) {
        write_index = (ring->head + ring->count) % CONVERSATION_CONTEXT_CAPACITY;
        ring->count++;
    } else {
        // The window is full, so the oldest slot is overwritten and counted as
        // an eviction to keep the loss observable.
        write_index = ring->head;
        ring->head = (ring->head + 1) % CONVERSATION_CONTEXT_CAPACITY;
        ring->evicted++;
    }

    conversation_context_turn_t *turn = &ring->turns[write_index];
    memset(turn, 0, sizeof(*turn));
    if (role != NULL && role[0] != '\0') {
        size_t copied = strlen(role);
        if (copied >= CONVERSATION_CONTEXT_ROLE_SIZE) {
            copied = CONVERSATION_CONTEXT_ROLE_SIZE - 1;
        }
        memcpy(turn->role, role, copied);
    } else {
        memcpy(turn->role, "user", 4);
    }

    size_t summary_length = strlen(summary);
    if (summary_length > CONVERSATION_CONTEXT_SUMMARY_SIZE) {
        summary_length = CONVERSATION_CONTEXT_SUMMARY_SIZE;
    }
    memcpy(turn->summary, summary, summary_length);
    turn->summary[summary_length] = '\0';
    turn->sequence = ring->next_sequence++;
    turn->created_at_ms = created_at_ms;
    ring->total_appended++;
    return true;
}

size_t conversation_context_ring_recent(
    const conversation_context_ring_t *ring,
    conversation_context_turn_t *turns_out,
    size_t max_turns
) {
    if (ring == NULL || turns_out == NULL || max_turns == 0) {
        return 0;
    }

    size_t available = ring->count;
    if (max_turns > available) {
        max_turns = available;
    }

    // Return the newest max_turns entries but keep chronological order: the
    // first written entry is the oldest of the returned window.
    const size_t start_offset = available - max_turns;
    for (size_t index = 0; index < max_turns; ++index) {
        const size_t slot =
            (ring->head + start_offset + index) % CONVERSATION_CONTEXT_CAPACITY;
        turns_out[index] = ring->turns[slot];
    }
    return max_turns;
}
