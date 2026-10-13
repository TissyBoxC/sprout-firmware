#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "playback_history.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure playback-history ring kept newest-first. */
typedef struct {
    playback_record_t records[PLAYBACK_HISTORY_CAPACITY];
    size_t count;
    // Bounded FIFO of records the platform has not accepted yet, oldest first,
    // keyed by (package id, played_at) so a repeated report never duplicates.
    playback_record_t pending[PLAYBACK_HISTORY_OUTBOUND_CAPACITY];
    size_t pending_count;
} playback_history_core_t;

/** @brief Reset the ring to empty. */
void playback_history_core_init(playback_history_core_t *core);

/** @brief Record one event, dropping the oldest record when full. */
playback_history_error_t playback_history_core_record(
    playback_history_core_t *core,
    const playback_record_t *record
);

/** @brief Return the newest interrupted record, if any. */
bool playback_history_core_latest_resumable(
    const playback_history_core_t *core,
    playback_record_t *record_out
);

/**
 * @brief Queue one recorded play for later reporting.
 *
 * The queue keeps at most one entry per (package id, played_at): a repeat of
 * the same play replaces the stored row in place so the oldest entry keeps its
 * position and a retried report never duplicates. When the bounded queue is
 * full the oldest entry is dropped to make room, because the most recent plays
 * are the ones the guardian still needs to see.
 */
playback_history_error_t playback_history_core_queue_pending(
    playback_history_core_t *core,
    const playback_record_t *record
);

/** @brief Copy the unreported records, oldest first. */
size_t playback_history_core_list_pending(
    const playback_history_core_t *core,
    playback_record_t *records_out,
    size_t capacity
);

/**
 * @brief Remove one pending record, keyed by (package id, played_at).
 *
 * A missing entry is success so a retried acknowledgement never fails.
 */
playback_history_error_t playback_history_core_remove_pending(
    playback_history_core_t *core,
    const char *package_id,
    int64_t played_at
);

#ifdef __cplusplus
}
#endif
