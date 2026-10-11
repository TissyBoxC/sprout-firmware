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

#ifdef __cplusplus
}
#endif
