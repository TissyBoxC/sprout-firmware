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

// Find the pending record for one (package id, played_at), or pending_count
// when there is none.
static size_t playback_history_core_find_pending(
    const playback_history_core_t *core,
    const char *package_id,
    int64_t played_at
) {
    if (core == NULL || package_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->pending_count; ++index) {
        if (core->pending[index].played_at == played_at &&
            strcmp(core->pending[index].package_id, package_id) == 0) {
            return index;
        }
    }
    return core->pending_count;
}

playback_history_error_t playback_history_core_queue_pending(
    playback_history_core_t *core,
    const playback_record_t *record
) {
    if (core == NULL) {
        return PLAYBACK_HISTORY_ERR_NOT_INITIALIZED;
    }
    if (!playback_record_is_valid(record)) {
        return PLAYBACK_HISTORY_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = playback_history_core_find_pending(
        core,
        record->package_id,
        record->played_at
    );
    if (existing < core->pending_count) {
        // Replace in place so a retried report reports once, with the latest
        // row, and keeps its position in the queue.
        core->pending[existing] = *record;
        return PLAYBACK_HISTORY_OK;
    }
    if (core->pending_count >= PLAYBACK_HISTORY_OUTBOUND_CAPACITY) {
        // Drop the oldest so the newest play still fits; the most recent plays
        // are the ones a guardian still needs to see.
        for (size_t index = 1; index < core->pending_count; ++index) {
            core->pending[index - 1] = core->pending[index];
        }
        core->pending_count -= 1;
    }
    core->pending[core->pending_count] = *record;
    core->pending_count += 1;
    return PLAYBACK_HISTORY_OK;
}

size_t playback_history_core_list_pending(
    const playback_history_core_t *core,
    playback_record_t *records_out,
    size_t capacity
) {
    if (core == NULL) {
        return 0;
    }
    const size_t copied = core->pending_count < capacity
        ? core->pending_count
        : capacity;
    if (records_out != NULL) {
        for (size_t index = 0; index < copied; ++index) {
            records_out[index] = core->pending[index];
        }
    }
    return core->pending_count;
}

playback_history_error_t playback_history_core_remove_pending(
    playback_history_core_t *core,
    const char *package_id,
    int64_t played_at
) {
    if (core == NULL) {
        return PLAYBACK_HISTORY_ERR_NOT_INITIALIZED;
    }
    if (package_id == NULL || package_id[0] == '\0') {
        return PLAYBACK_HISTORY_ERR_INVALID_ARGUMENT;
    }
    const size_t index =
        playback_history_core_find_pending(core, package_id, played_at);
    if (index >= core->pending_count) {
        return PLAYBACK_HISTORY_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->pending_count; ++cursor) {
        core->pending[cursor] = core->pending[cursor + 1];
    }
    core->pending_count -= 1;
    memset(
        &core->pending[core->pending_count],
        0,
        sizeof(playback_record_t)
    );
    return PLAYBACK_HISTORY_OK;
}
