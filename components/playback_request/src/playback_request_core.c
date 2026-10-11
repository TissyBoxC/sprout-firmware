#include "playback_request_core.h"

#include <string.h>

void playback_request_core_init(playback_request_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

bool playback_request_status_is_terminal(playback_request_status_t status) {
    return status == PLAYBACK_REQUEST_COMPLETED ||
        status == PLAYBACK_REQUEST_FAILED ||
        status == PLAYBACK_REQUEST_CANCELLED;
}

static bool playback_request_status_is_valid(playback_request_status_t status) {
    switch (status) {
        case PLAYBACK_REQUEST_PENDING:
        case PLAYBACK_REQUEST_ACKNOWLEDGED:
        case PLAYBACK_REQUEST_COMPLETED:
        case PLAYBACK_REQUEST_FAILED:
        case PLAYBACK_REQUEST_CANCELLED:
            return true;
        default:
            return false;
    }
}

static bool playback_request_outcome_is_valid(
    playback_request_outcome_t outcome
) {
    switch (outcome) {
        case PLAYBACK_REQUEST_OUTCOME_NONE:
        case PLAYBACK_REQUEST_OUTCOME_PLAYED:
        case PLAYBACK_REQUEST_OUTCOME_SKIPPED:
        case PLAYBACK_REQUEST_OUTCOME_ERROR:
            return true;
        default:
            return false;
    }
}

static bool playback_request_is_valid(const playback_request_t *request) {
    if (request == NULL) {
        return false;
    }
    if (request->request_id[0] == '\0' || request->package_id[0] == '\0') {
        return false;
    }
    if (!playback_request_status_is_valid(request->status)) {
        return false;
    }
    return playback_request_outcome_is_valid(request->outcome);
}

size_t playback_request_core_find(
    const playback_request_core_t *core,
    const char *request_id
) {
    if (core == NULL || request_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->count; ++index) {
        if (strcmp(core->requests[index].request_id, request_id) == 0) {
            return index;
        }
    }
    return core->count;
}

playback_request_error_t playback_request_core_upsert(
    playback_request_core_t *core,
    const playback_request_t *request
) {
    if (core == NULL) {
        return PLAYBACK_REQUEST_ERR_NOT_INITIALIZED;
    }
    if (!playback_request_is_valid(request)) {
        return PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = playback_request_core_find(
        core,
        request->request_id
    );
    if (existing >= core->count && core->count >= PLAYBACK_REQUEST_CAPACITY) {
        return PLAYBACK_REQUEST_ERR_FULL;
    }
    if (existing < core->count) {
        for (size_t index = existing; index > 0; --index) {
            core->requests[index] = core->requests[index - 1];
        }
        core->requests[0] = *request;
        return PLAYBACK_REQUEST_OK;
    }
    const size_t last = core->count < PLAYBACK_REQUEST_CAPACITY
        ? core->count
        : PLAYBACK_REQUEST_CAPACITY - 1;
    for (size_t index = last; index > 0; --index) {
        core->requests[index] = core->requests[index - 1];
    }
    core->requests[0] = *request;
    core->count += 1;
    return PLAYBACK_REQUEST_OK;
}

playback_request_error_t playback_request_core_remove(
    playback_request_core_t *core,
    const char *request_id
) {
    if (core == NULL) {
        return PLAYBACK_REQUEST_ERR_NOT_INITIALIZED;
    }
    if (request_id == NULL || request_id[0] == '\0') {
        return PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT;
    }
    const size_t index = playback_request_core_find(core, request_id);
    if (index >= core->count) {
        return PLAYBACK_REQUEST_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->count; ++cursor) {
        core->requests[cursor] = core->requests[cursor + 1];
    }
    core->count -= 1;
    memset(
        &core->requests[core->count],
        0,
        sizeof(playback_request_t)
    );
    return PLAYBACK_REQUEST_OK;
}

playback_request_error_t playback_request_core_acknowledge(
    playback_request_core_t *core,
    const char *request_id
) {
    if (core == NULL) {
        return PLAYBACK_REQUEST_ERR_NOT_INITIALIZED;
    }
    if (request_id == NULL || request_id[0] == '\0') {
        return PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT;
    }
    const size_t index = playback_request_core_find(core, request_id);
    if (index >= core->count) {
        return PLAYBACK_REQUEST_ERR_NOT_FOUND;
    }
    if (playback_request_status_is_terminal(core->requests[index].status)) {
        return PLAYBACK_REQUEST_ERR_INVALID_STATE;
    }
    core->requests[index].status = PLAYBACK_REQUEST_ACKNOWLEDGED;
    return PLAYBACK_REQUEST_OK;
}

playback_request_error_t playback_request_core_complete(
    playback_request_core_t *core,
    const char *request_id,
    playback_request_outcome_t outcome
) {
    if (core == NULL) {
        return PLAYBACK_REQUEST_ERR_NOT_INITIALIZED;
    }
    if (request_id == NULL || request_id[0] == '\0') {
        return PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT;
    }
    if (outcome == PLAYBACK_REQUEST_OUTCOME_NONE ||
        !playback_request_outcome_is_valid(outcome)) {
        return PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT;
    }
    const size_t index = playback_request_core_find(core, request_id);
    if (index >= core->count) {
        return PLAYBACK_REQUEST_ERR_NOT_FOUND;
    }
    if (playback_request_status_is_terminal(core->requests[index].status)) {
        return PLAYBACK_REQUEST_ERR_INVALID_STATE;
    }
    core->requests[index].outcome = outcome;
    core->requests[index].status = outcome == PLAYBACK_REQUEST_OUTCOME_ERROR
        ? PLAYBACK_REQUEST_FAILED
        : PLAYBACK_REQUEST_COMPLETED;
    return PLAYBACK_REQUEST_OK;
}
