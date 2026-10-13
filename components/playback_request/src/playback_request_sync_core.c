#include "playback_request_sync_core.h"

#include <string.h>

static bool playback_request_sync_is_valid(
    const playback_request_t *request
) {
    if (request == NULL) {
        return false;
    }
    playback_request_core_t probe;
    playback_request_core_init(&probe);
    return playback_request_core_upsert(&probe, request) ==
        PLAYBACK_REQUEST_OK;
}

playback_request_error_t playback_request_sync_core_merge(
    playback_request_core_t *core,
    const playback_request_t *remote,
    size_t remote_count
) {
    if (core == NULL) {
        return PLAYBACK_REQUEST_ERR_NOT_INITIALIZED;
    }
    if (remote == NULL && remote_count > 0) {
        return PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT;
    }

    // Validate and de-duplicate the remote snapshot first so one bad entry
    // cannot abort an otherwise usable sync.
    playback_request_t accepted[PLAYBACK_REQUEST_CAPACITY] = {};
    size_t accepted_count = 0;
    for (size_t index = 0; index < remote_count; ++index) {
        const playback_request_t *request = &remote[index];
        if (!playback_request_sync_is_valid(request)) {
            continue;
        }
        if (accepted_count >= PLAYBACK_REQUEST_CAPACITY) {
            break;
        }
        bool duplicate = false;
        for (size_t probe = 0; probe < accepted_count; ++probe) {
            if (strcmp(accepted[probe].request_id, request->request_id) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        accepted[accepted_count] = *request;
        accepted_count += 1;
    }

    playback_request_core_t rebuilt;
    playback_request_core_init(&rebuilt);

    // Retain local terminal history the remote snapshot no longer carries.
    for (size_t index = 0; index < core->count; ++index) {
        const playback_request_t *local = &core->requests[index];
        if (!playback_request_status_is_terminal(local->status)) {
            continue;
        }
        (void)playback_request_core_upsert(&rebuilt, local);
    }

    // Insert remote requests the device has not already finished. A remote
    // entry the local cache already finished is left alone so a played request
    // is not resurrected as pending.
    for (size_t index = 0; index < accepted_count; ++index) {
        const playback_request_t *request = &accepted[index];
        const size_t existing = playback_request_core_find(
            core,
            request->request_id
        );
        if (existing < core->count &&
            playback_request_status_is_terminal(
                core->requests[existing].status
            )) {
            continue;
        }
        if (playback_request_core_upsert(&rebuilt, request) ==
            PLAYBACK_REQUEST_ERR_FULL) {
            break;
        }
    }

    *core = rebuilt;
    return PLAYBACK_REQUEST_OK;
}
