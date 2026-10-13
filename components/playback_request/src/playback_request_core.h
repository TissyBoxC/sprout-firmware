#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "playback_request.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure playback request store kept newest-change-first. */
typedef struct {
    playback_request_t requests[PLAYBACK_REQUEST_CAPACITY];
    size_t count;
    // Bounded FIFO of local lifecycle changes the platform has not accepted
    // yet. An acknowledgement and a completion for one request are separate
    // entries because the platform advances the request one step at a time.
    playback_request_outbound_t outbound[PLAYBACK_REQUEST_OUTBOUND_CAPACITY];
    size_t outbound_count;
} playback_request_core_t;

/** @brief Reset the store to empty. */
void playback_request_core_init(playback_request_core_t *core);

/** @brief Insert or replace one request, moving it to the head. */
playback_request_error_t playback_request_core_upsert(
    playback_request_core_t *core,
    const playback_request_t *request
);

/** @brief Remove one request; a missing id is success. */
playback_request_error_t playback_request_core_remove(
    playback_request_core_t *core,
    const char *request_id
);

/** @brief Mark one request acknowledged, rejecting a terminal request. */
playback_request_error_t playback_request_core_acknowledge(
    playback_request_core_t *core,
    const char *request_id
);

/** @brief Record the terminal outcome for one request. */
playback_request_error_t playback_request_core_complete(
    playback_request_core_t *core,
    const char *request_id,
    playback_request_outcome_t outcome
);

/** @brief Find one request index, or count when absent. */
size_t playback_request_core_find(
    const playback_request_core_t *core,
    const char *request_id
);

/** @brief Report whether one status is terminal. */
bool playback_request_status_is_terminal(playback_request_status_t status);

/**
 * @brief Queue one outbound lifecycle change for the platform.
 *
 * A request keeps at most one queued acknowledgement and one queued completion:
 * a repeat of the same kind replaces the stored outcome in place so the oldest
 * entry keeps its position and a retried transition never duplicates. A
 * completion supersedes a still-queued acknowledgement because the platform
 * accepts a completion from either the pending or acknowledged state. When the
 * bounded queue is full the oldest entry is dropped to make room, because the
 * most recent changes are the ones the guardian still needs to see.
 */
playback_request_error_t playback_request_core_queue_outbound(
    playback_request_core_t *core,
    const char *request_id,
    playback_request_outbound_kind_t kind,
    playback_request_outcome_t outcome
);

/** @brief Copy the unacknowledged outbound actions, oldest first. */
size_t playback_request_core_pending_outbound(
    const playback_request_core_t *core,
    playback_request_outbound_t *actions_out,
    size_t capacity
);

/**
 * @brief Remove one outbound action, keyed by (request id, kind).
 *
 * A missing entry is success so a retried acknowledgement never fails.
 */
playback_request_error_t playback_request_core_remove_outbound(
    playback_request_core_t *core,
    const char *request_id,
    playback_request_outbound_kind_t kind
);

#ifdef __cplusplus
}
#endif
