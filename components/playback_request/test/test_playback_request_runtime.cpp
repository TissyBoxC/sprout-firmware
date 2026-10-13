#include "playback_request.h"

#include <cassert>
#include <cstring>

static playback_request_t make_request(
    const char *request_id,
    const char *package_id
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
    request.status = PLAYBACK_REQUEST_PENDING;
    request.outcome = PLAYBACK_REQUEST_OUTCOME_NONE;
    request.requested_at_epoch_seconds = 1700000000;
    return request;
}

int main() {
    assert(playback_request_init() == ESP_OK);
    assert(playback_request_is_ready());
    assert(playback_request_count() == 0);

    const playback_request_t first = make_request("req_001", "lesson_story");
    assert(playback_request_upsert(&first) == ESP_OK);
    const playback_request_t second = make_request("req_002", "lesson_song");
    assert(playback_request_upsert(&second) == ESP_OK);
    assert(playback_request_count() == 2);

    playback_request_t recalled;
    std::memset(&recalled, 0, sizeof(recalled));
    assert(playback_request_get("req_001", &recalled));
    assert(std::strcmp(recalled.package_id, "lesson_story") == 0);
    assert(!playback_request_get("req_missing", &recalled));

    // Pending list is oldest-first regardless of the newest-first store.
    playback_request_t pending[PLAYBACK_REQUEST_CAPACITY];
    size_t pending_count = 0;
    assert(
        playback_request_list_pending(
            pending,
            PLAYBACK_REQUEST_CAPACITY,
            &pending_count
        ) == ESP_OK
    );
    assert(pending_count == 2);
    assert(std::strcmp(pending[0].request_id, "req_001") == 0);
    assert(std::strcmp(pending[1].request_id, "req_002") == 0);

    // Acknowledge then complete drops the request from the pending list.
    assert(playback_request_acknowledge("req_001") == ESP_OK);
    assert(
        playback_request_complete(
            "req_001",
            PLAYBACK_REQUEST_OUTCOME_PLAYED
        ) == ESP_OK
    );
    // Completing an already-terminal request reports an invalid state.
    assert(
        playback_request_complete(
            "req_001",
            PLAYBACK_REQUEST_OUTCOME_SKIPPED
        ) == ESP_ERR_INVALID_STATE
    );
    // An unknown request reports not found.
    assert(playback_request_acknowledge("req_missing") == ESP_ERR_NOT_FOUND);

    // Acknowledging and completing queue outbound actions for the platform, and
    // the completion supersedes the acknowledgement for the same request.
    assert(playback_request_pending_outbound_count() == 1u);
    playback_request_outbound_t outbound[PLAYBACK_REQUEST_OUTBOUND_CAPACITY];
    size_t outbound_count = 0;
    assert(
        playback_request_list_pending_outbound(
            outbound,
            PLAYBACK_REQUEST_OUTBOUND_CAPACITY,
            &outbound_count
        ) == ESP_OK
    );
    assert(outbound_count == 1u);
    assert(std::strcmp(outbound[0].request_id, "req_001") == 0);
    assert(outbound[0].kind == PLAYBACK_REQUEST_OUTBOUND_COMPLETE);
    assert(outbound[0].outcome == PLAYBACK_REQUEST_OUTCOME_PLAYED);
    // Dropping the reported action drains the queue; a repeat ack is success.
    assert(
        playback_request_ack_outbound(
            "req_001",
            PLAYBACK_REQUEST_OUTBOUND_COMPLETE
        ) == ESP_OK
    );
    assert(
        playback_request_ack_outbound(
            "req_001",
            PLAYBACK_REQUEST_OUTBOUND_COMPLETE
        ) == ESP_OK
    );
    assert(playback_request_pending_outbound_count() == 0u);

    assert(
        playback_request_list_pending(
            pending,
            PLAYBACK_REQUEST_CAPACITY,
            &pending_count
        ) == ESP_OK
    );
    assert(pending_count == 1);
    assert(std::strcmp(pending[0].request_id, "req_002") == 0);

    // The blob survives a re-init, standing in for a device restart.
    assert(playback_request_init() == ESP_OK);
    assert(playback_request_count() == 2);
    // The queue is persisted too, so a drained action stays drained across a
    // restart.
    assert(playback_request_pending_outbound_count() == 0u);
    assert(playback_request_get("req_001", &recalled));
    assert(recalled.status == PLAYBACK_REQUEST_COMPLETED);
    assert(recalled.outcome == PLAYBACK_REQUEST_OUTCOME_PLAYED);

    // Removing a request persists.
    assert(playback_request_remove("req_002") == ESP_OK);
    assert(playback_request_count() == 1);
    assert(playback_request_init() == ESP_OK);
    assert(playback_request_count() == 1);
    assert(playback_request_get("req_002", &recalled) == false);

    assert(
        std::strcmp(
            playback_request_status_name(PLAYBACK_REQUEST_ACKNOWLEDGED),
            "acknowledged"
        ) == 0
    );
    playback_request_status_t parsed_status;
    assert(playback_request_status_from_name("cancelled", &parsed_status));
    assert(parsed_status == PLAYBACK_REQUEST_CANCELLED);
    assert(!playback_request_status_from_name("bogus", &parsed_status));

    assert(
        std::strcmp(
            playback_request_outcome_name(PLAYBACK_REQUEST_OUTCOME_SKIPPED),
            "skipped"
        ) == 0
    );
    playback_request_outcome_t parsed_outcome;
    assert(playback_request_outcome_from_name("error", &parsed_outcome));
    assert(parsed_outcome == PLAYBACK_REQUEST_OUTCOME_ERROR);
    assert(!playback_request_outcome_from_name("bogus", &parsed_outcome));

    return 0;
}
