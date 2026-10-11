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

#ifdef __cplusplus
}
#endif
