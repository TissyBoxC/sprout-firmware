#include "playback_history_core.h"

#include <string.h>

void playback_history_core_init(playback_history_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

static bool playback_record_is_valid(const playback_record_t *record) {
    if (record == NULL) {
        return false;
    }
    if (record->package_id[0] == '\0') {
        return false;
    }
    if (record->package_version < 1) {
        return false;
    }
    // A single session cannot exceed one day; the platform enforces the same
    // bound so a bad firmware value cannot create an unbounded row.
    if (record->played_seconds > 86400) {
        return false;
    }
    return true;
}

playback_history_error_t playback_history_core_record(
    playback_history_core_t *core,
    const playback_record_t *record
) {
    if (core == NULL) {
        return PLAYBACK_HISTORY_ERR_NOT_INITIALIZED;
    }
    if (!playback_record_is_valid(record)) {
        return PLAYBACK_HISTORY_ERR_INVALID_ARGUMENT;
    }
    // Shift one slot toward the tail, dropping the oldest record when full.
    const size_t last = core->count < PLAYBACK_HISTORY_CAPACITY
        ? core->count
        : PLAYBACK_HISTORY_CAPACITY - 1;
    for (size_t index = last; index > 0; --index) {
        core->records[index] = core->records[index - 1];
    }
    core->records[0] = *record;
    if (core->count < PLAYBACK_HISTORY_CAPACITY) {
        core->count += 1;
    }
    return PLAYBACK_HISTORY_OK;
}

bool playback_history_core_latest_resumable(
    const playback_history_core_t *core,
    playback_record_t *record_out
) {
    if (core == NULL || record_out == NULL) {
        return false;
    }
    // Records are newest-first, so the first interrupted row is the newest.
    for (size_t index = 0; index < core->count; ++index) {
        if (core->records[index].outcome == PLAYBACK_OUTCOME_INTERRUPTED) {
            *record_out = core->records[index];
            return true;
        }
    }
    return false;
}
