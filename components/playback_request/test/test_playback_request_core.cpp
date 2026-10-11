#include "playback_request_core.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static playback_request_t make_request(
    const char *request_id,
    const char *package_id,
    playback_request_status_t status
) {
    playback_request_t request;
    std::memset(&request, 0, sizeof(request));
    std::strncpy(
        request.request_id,
        request_id,
        PLAYBACK_REQUEST_ID_SIZE - 1
    );
    std::strncpy(
        request.package_id,
        package_id,
        PLAYBACK_REQUEST_PACKAGE_ID_SIZE - 1
    );
    request.status = status;
    request.outcome = PLAYBACK_REQUEST_OUTCOME_NONE;
    request.requested_at_epoch_seconds = 1700000000;
    return request;
}

int main() {
    playback_request_core_t core;
    playback_request_core_init(&core);
    assert(core.count == 0);

    const playback_request_t first = make_request(
        "req_001",
        "lesson_story",
        PLAYBACK_REQUEST_PENDING
    );
    assert(
        playback_request_core_upsert(&core, &first) == PLAYBACK_REQUEST_OK
    );
    assert(core.count == 1);
    assert(std::strcmp(core.requests[0].request_id, "req_001") == 0);

    const playback_request_t second = make_request(
        "req_002",
        "lesson_song",
        PLAYBACK_REQUEST_PENDING
    );
    assert(
        playback_request_core_upsert(&core, &second) == PLAYBACK_REQUEST_OK
    );
    assert(core.count == 2);
    // Newest change leads.
    assert(std::strcmp(core.requests[0].request_id, "req_002") == 0);

    // Re-upserting an existing request replaces it and moves it to the head.
    playback_request_t first_v2 = make_request(
        "req_001",
        "lesson_story",
        PLAYBACK_REQUEST_PENDING
    );
    assert(
        playback_request_core_upsert(&core, &first_v2) == PLAYBACK_REQUEST_OK
    );
    assert(core.count == 2);
    assert(std::strcmp(core.requests[0].request_id, "req_001") == 0);

    // Find reports the index, or the count when absent.
    assert(playback_request_core_find(&core, "req_001") == 0);
    assert(playback_request_core_find(&core, "req_002") == 1);
    assert(playback_request_core_find(&core, "req_missing") == core.count);

    // Invalid requests are rejected.
    playback_request_t invalid = make_request(
        "",
        "lesson_story",
        PLAYBACK_REQUEST_PENDING
    );
    assert(
        playback_request_core_upsert(&core, &invalid) ==
        PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT
    );
    invalid = make_request("req_003", "lesson_story", PLAYBACK_REQUEST_PENDING);
    invalid.status = static_cast<playback_request_status_t>(99);
    assert(
        playback_request_core_upsert(&core, &invalid) ==
        PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT
    );

    // Acknowledge then complete moves the request to its terminal state.
    assert(
        playback_request_core_acknowledge(&core, "req_002") ==
        PLAYBACK_REQUEST_OK
    );
    assert(
        playback_request_core_find(&core, "req_002") == 1
    );
    assert(core.requests[1].status == PLAYBACK_REQUEST_ACKNOWLEDGED);
    assert(
        playback_request_core_complete(
            &core,
            "req_002",
            PLAYBACK_REQUEST_OUTCOME_PLAYED
        ) == PLAYBACK_REQUEST_OK
    );
    assert(core.requests[1].status == PLAYBACK_REQUEST_COMPLETED);
    assert(core.requests[1].outcome == PLAYBACK_REQUEST_OUTCOME_PLAYED);

    // A terminal request is not changed again.
    assert(
        playback_request_core_acknowledge(&core, "req_002") ==
        PLAYBACK_REQUEST_ERR_INVALID_STATE
    );
    assert(
        playback_request_core_complete(
            &core,
            "req_002",
            PLAYBACK_REQUEST_OUTCOME_SKIPPED
        ) == PLAYBACK_REQUEST_ERR_INVALID_STATE
    );

    // Completing with ERROR fails the request.
    assert(
        playback_request_core_complete(
            &core,
            "req_001",
            PLAYBACK_REQUEST_OUTCOME_ERROR
        ) == PLAYBACK_REQUEST_OK
    );
    assert(core.requests[0].status == PLAYBACK_REQUEST_FAILED);

    // An unknown request id is NOT_FOUND.
    assert(
        playback_request_core_acknowledge(&core, "req_missing") ==
        PLAYBACK_REQUEST_ERR_NOT_FOUND
    );

    // An invalid outcome is rejected.
    playback_request_core_t fresh;
    playback_request_core_init(&fresh);
    const playback_request_t only = make_request(
        "req_010",
        "lesson_story",
        PLAYBACK_REQUEST_PENDING
    );
    assert(
        playback_request_core_upsert(&fresh, &only) == PLAYBACK_REQUEST_OK
    );
    assert(
        playback_request_core_complete(
            &fresh,
            "req_010",
            PLAYBACK_REQUEST_OUTCOME_NONE
        ) == PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT
    );

    // Removing a missing request is success.
    assert(
        playback_request_core_remove(&core, "req_missing") ==
        PLAYBACK_REQUEST_OK
    );
    assert(
        playback_request_core_remove(&core, "req_001") == PLAYBACK_REQUEST_OK
    );
    assert(core.count == 1);

    assert(!playback_request_status_is_terminal(PLAYBACK_REQUEST_PENDING));
    assert(!playback_request_status_is_terminal(PLAYBACK_REQUEST_ACKNOWLEDGED));
    assert(playback_request_status_is_terminal(PLAYBACK_REQUEST_COMPLETED));
    assert(playback_request_status_is_terminal(PLAYBACK_REQUEST_FAILED));
    assert(playback_request_status_is_terminal(PLAYBACK_REQUEST_CANCELLED));

    // Capacity is bounded: inserting past the limit reports FULL.
    playback_request_core_t full;
    playback_request_core_init(&full);
    char id[PLAYBACK_REQUEST_ID_SIZE];
    for (int index = 0; index < PLAYBACK_REQUEST_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "req_%03d", index);
        const playback_request_t request = make_request(
            id,
            "lesson_story",
            PLAYBACK_REQUEST_PENDING
        );
        assert(
            playback_request_core_upsert(&full, &request) ==
            PLAYBACK_REQUEST_OK
        );
    }
    assert(full.count == PLAYBACK_REQUEST_CAPACITY);
    const playback_request_t overflow = make_request(
        "req_overflow",
        "lesson_story",
        PLAYBACK_REQUEST_PENDING
    );
    assert(
        playback_request_core_upsert(&full, &overflow) ==
        PLAYBACK_REQUEST_ERR_FULL
    );

    return 0;
}
