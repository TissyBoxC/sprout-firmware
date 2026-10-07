#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "conversation_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pure bounded ring buffer for recent turn summaries.
 *
 * This structure has no ESP-IDF dependencies so the window semantics can be
 * exercised on the host. The module wrapper adds the mutex and lifecycle.
 */
typedef struct {
    conversation_context_turn_t turns[CONVERSATION_CONTEXT_CAPACITY];
    uint32_t next_sequence;
    uint32_t total_appended;
    uint32_t evicted;
    size_t head;
    size_t count;
} conversation_context_ring_t;

/** @brief Reset a ring to the empty state. */
void conversation_context_ring_reset(conversation_context_ring_t *ring);

/**
 * @brief Append one turn, evicting the oldest entry when full.
 *
 * Returns false for a NULL ring or an empty summary and true on success.
 */
bool conversation_context_ring_append(
    conversation_context_ring_t *ring,
    const char *role,
    const char *summary,
    uint64_t created_at_ms
);

/**
 * @brief Copy the newest max_turns entries ending at the newest turn.
 *
 * Returns the number of written entries. max_turns is clamped so a caller can
 * never read past the ring.
 */
size_t conversation_context_ring_recent(
    const conversation_context_ring_t *ring,
    conversation_context_turn_t *turns_out,
    size_t max_turns
);

#ifdef __cplusplus
}
#endif
