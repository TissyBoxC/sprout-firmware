#include "playback_request_sync_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static playback_request_t make_request(
    const char *request_id,
    const char *package_id,
    playback_request_status_t status
) {
    playback_request_t request = {};
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
    playback_request_core_t core = {};
    playback_request_core_init(&core);

    // Local history: one finished request and one still-pending request the
    // guardian is about to cancel.
    playback_request_t played = make_request(
        "req_played",
        "content_story_001",
        PLAYBACK_REQUEST_COMPLETED
    );
    played.outcome = PLAYBACK_REQUEST_OUTCOME_PLAYED;
    assert(playback_request_core_upsert(&core, &played) == PLAYBACK_REQUEST_OK);
    playback_request_t cancelled = make_request(
        "req_cancelled",
        "content_story_002",
        PLAYBACK_REQUEST_PENDING
    );
    assert(
        playback_request_core_upsert(&core, &cancelled) == PLAYBACK_REQUEST_OK
    );

    // Remote pending view: re-lists the played request, keeps one request, and
    // adds a brand-new one. The cancelled request is absent.
    playback_request_t remote[] = {
        make_request(
            "req_played",
            "content_story_001",
            PLAYBACK_REQUEST_PENDING
        ),
        make_request(
            "req_keep",
            "content_story_003",
            PLAYBACK_REQUEST_ACKNOWLEDGED
        ),
        make_request(
            "req_new",
            "content_story_004",
            PLAYBACK_REQUEST_PENDING
        ),
    };
    assert(
        playback_request_sync_core_merge(&core, remote, 3) ==
        PLAYBACK_REQUEST_OK
    );
    // Played history retained, cancelled pending dropped, remote additions in.
    assert(playback_request_core_find(&core, "req_played") < core.count);
    assert(
        core.requests[playback_request_core_find(&core, "req_played")].status ==
        PLAYBACK_REQUEST_COMPLETED
    );
    assert(playback_request_core_find(&core, "req_cancelled") == core.count);
    assert(playback_request_core_find(&core, "req_keep") < core.count);
    assert(playback_request_core_find(&core, "req_new") < core.count);
    assert(
        core.requests[playback_request_core_find(&core, "req_keep")].status ==
        PLAYBACK_REQUEST_ACKNOWLEDGED
    );

    // An empty remote view drops every local pending request but keeps history.
    assert(playback_request_sync_core_merge(&core, nullptr, 0) == PLAYBACK_REQUEST_OK);
    assert(playback_request_core_find(&core, "req_new") == core.count);
    assert(playback_request_core_find(&core, "req_played") < core.count);

    // Invalid remote ids are dropped without aborting the merge.
    playback_request_t invalid[] = {
        make_request("", "content_story_005", PLAYBACK_REQUEST_PENDING),
        make_request("req_ok", "", PLAYBACK_REQUEST_PENDING),
        make_request("req_good", "content_story_006", PLAYBACK_REQUEST_PENDING),
    };
    assert(
        playback_request_sync_core_merge(&core, invalid, 3) ==
        PLAYBACK_REQUEST_OK
    );
    assert(playback_request_core_find(&core, "req_good") < core.count);
    assert(playback_request_core_find(&core, "req_ok") == core.count);

    // Capacity is bounded.
    playback_request_t too_many[PLAYBACK_REQUEST_CAPACITY + 2] = {};
    char ids[PLAYBACK_REQUEST_CAPACITY + 2][PLAYBACK_REQUEST_ID_SIZE] = {};
    for (int index = 0; index < PLAYBACK_REQUEST_CAPACITY + 2; ++index) {
        std::snprintf(ids[index], sizeof(ids[index]), "req_%03d", index);
        too_many[index] = make_request(
            ids[index],
            "content_story_007",
            PLAYBACK_REQUEST_PENDING
        );
    }
    assert(
        playback_request_sync_core_merge(
            &core,
            too_many,
            PLAYBACK_REQUEST_CAPACITY + 2
        ) == PLAYBACK_REQUEST_OK
    );
    assert(core.count <= PLAYBACK_REQUEST_CAPACITY);

    return 0;
}
