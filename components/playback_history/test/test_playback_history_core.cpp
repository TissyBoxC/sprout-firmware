#include "playback_history_core.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static playback_record_t make_record(
    const char *package_id,
    playback_outcome_t outcome,
    uint32_t seconds,
    int64_t played_at
) {
    playback_record_t record = {};
    std::strncpy(record.package_id, package_id, CONTENT_LIBRARY_ID_SIZE - 1);
    record.package_version = 1;
    record.outcome = outcome;
    record.played_seconds = seconds;
    record.played_at = played_at;
    return record;
}

int main() {
    playback_history_core_t core = {};
    playback_history_core_init(&core);
    assert(core.count == 0);

    playback_record_t first = make_record(
        "content_story_001",
        PLAYBACK_OUTCOME_COMPLETED,
        120,
        1000
    );
    assert(
        playback_history_core_record(&core, &first) == PLAYBACK_HISTORY_OK
    );
    playback_record_t second = make_record(
        "content_story_002",
        PLAYBACK_OUTCOME_INTERRUPTED,
        30,
        2000
    );
    assert(
        playback_history_core_record(&core, &second) == PLAYBACK_HISTORY_OK
    );
    assert(core.count == 2);
    // Newest-first ordering.
    assert(
        std::strcmp(core.records[0].package_id, "content_story_002") == 0
    );

    // The newest interrupted row is the resumable one.
    playback_record_t resumable = {};
    assert(playback_history_core_latest_resumable(&core, &resumable));
    assert(std::strcmp(resumable.package_id, "content_story_002") == 0);
    assert(resumable.played_seconds == 30);

    // A completed record is not resumable; a newer completed row hides the
    // older interrupted one only when it is the newest with that outcome.
    playback_record_t third = make_record(
        "content_story_001",
        PLAYBACK_OUTCOME_COMPLETED,
        120,
        3000
    );
    assert(
        playback_history_core_record(&core, &third) == PLAYBACK_HISTORY_OK
    );
    assert(playback_history_core_latest_resumable(&core, &resumable));
    // The older interrupted row (story_002) is still the newest interrupted.
    assert(std::strcmp(resumable.package_id, "content_story_002") == 0);

    // Invalid records are rejected.
    playback_record_t invalid = make_record(
        "content_story_001",
        PLAYBACK_OUTCOME_COMPLETED,
        10,
        1
    );
    invalid.package_version = 0;
    assert(
        playback_history_core_record(&core, &invalid) ==
        PLAYBACK_HISTORY_ERR_INVALID_ARGUMENT
    );
    invalid = make_record("", PLAYBACK_OUTCOME_COMPLETED, 10, 1);
    assert(
        playback_history_core_record(&core, &invalid) ==
        PLAYBACK_HISTORY_ERR_INVALID_ARGUMENT
    );
    invalid = make_record(
        "content_story_001",
        PLAYBACK_OUTCOME_COMPLETED,
        86401,
        1
    );
    assert(
        playback_history_core_record(&core, &invalid) ==
        PLAYBACK_HISTORY_ERR_INVALID_ARGUMENT
    );

    // The ring is bounded: filling past capacity drops the oldest row.
    playback_history_core_t ring = {};
    playback_history_core_init(&ring);
    char id[CONTENT_LIBRARY_ID_SIZE];
    for (int index = 0; index < PLAYBACK_HISTORY_CAPACITY + 5; ++index) {
        std::snprintf(id, sizeof(id), "content_story_%03d", index);
        const playback_record_t record = make_record(
            id,
            PLAYBACK_OUTCOME_COMPLETED,
            10,
            index
        );
        assert(playback_history_core_record(&ring, &record) == PLAYBACK_HISTORY_OK);
    }
    assert(ring.count == PLAYBACK_HISTORY_CAPACITY);
    // The newest record leads and the oldest surviving id is the one at the
    // capacity boundary.
    assert(
        std::strcmp(
            ring.records[0].package_id,
            "content_story_036"
        ) == 0
    );
    assert(
        std::strcmp(
            ring.records[PLAYBACK_HISTORY_CAPACITY - 1].package_id,
            "content_story_005"
        ) == 0
    );

    // An all-completed ring has no resumable row.
    playback_record_t none = {};
    assert(!playback_history_core_latest_resumable(&ring, &none));

    // Recorded plays are queued for the platform oldest-first, and a repeat of
    // the same (package, played_at) replaces the stored row in place.
    playback_history_core_t queued = {};
    playback_history_core_init(&queued);
    assert(queued.pending_count == 0);
    playback_record_t play_one = make_record(
        "content_story_001",
        PLAYBACK_OUTCOME_COMPLETED,
        60,
        1000
    );
    assert(
        playback_history_core_queue_pending(&queued, &play_one) ==
        PLAYBACK_HISTORY_OK
    );
    assert(
        playback_history_core_queue_pending(&queued, &play_one) ==
        PLAYBACK_HISTORY_OK
    );
    assert(queued.pending_count == 1);
    playback_record_t play_two = make_record(
        "content_story_002",
        PLAYBACK_OUTCOME_INTERRUPTED,
        30,
        2000
    );
    assert(
        playback_history_core_queue_pending(&queued, &play_two) ==
        PLAYBACK_HISTORY_OK
    );
    assert(queued.pending_count == 2);
    // The same package played again at a different time is a distinct row.
    playback_record_t play_one_again = make_record(
        "content_story_001",
        PLAYBACK_OUTCOME_COMPLETED,
        90,
        3000
    );
    assert(
        playback_history_core_queue_pending(&queued, &play_one_again) ==
        PLAYBACK_HISTORY_OK
    );
    assert(queued.pending_count == 3);

    // The list helper copies oldest-first and reports the full count even when
    // the caller provides a smaller buffer.
    playback_record_t listed[PLAYBACK_HISTORY_OUTBOUND_CAPACITY] = {};
    const size_t listed_count =
        playback_history_core_list_pending(&queued, listed, 2);
    assert(listed_count == 3);
    assert(std::strcmp(listed[0].package_id, "content_story_001") == 0);
    assert(listed[0].played_at == 1000);

    // Removing one row drains exactly that (package, played_at) pair; a missing
    // entry is success so a retried report never fails.
    assert(
        playback_history_core_remove_pending(
            &queued,
            "content_story_001",
            1000
        ) == PLAYBACK_HISTORY_OK
    );
    assert(queued.pending_count == 2);
    assert(
        playback_history_core_remove_pending(
            &queued,
            "content_story_001",
            1000
        ) == PLAYBACK_HISTORY_OK
    );
    assert(queued.pending_count == 2);

    // The queue is bounded; filling past capacity drops the oldest row while
    // keeping the newest.
    playback_history_core_t filled = {};
    playback_history_core_init(&filled);
    for (int index = 0; index < PLAYBACK_HISTORY_OUTBOUND_CAPACITY + 4;
         ++index) {
        std::snprintf(id, sizeof(id), "content_story_%03d", index);
        const playback_record_t record = make_record(
            id,
            PLAYBACK_OUTCOME_COMPLETED,
            10,
            index
        );
        assert(
            playback_history_core_queue_pending(&filled, &record) ==
            PLAYBACK_HISTORY_OK
        );
    }
    assert(filled.pending_count == PLAYBACK_HISTORY_OUTBOUND_CAPACITY);
    assert(
        std::strcmp(
            filled.pending[filled.pending_count - 1].package_id,
            "content_story_035"
        ) == 0
    );

    return 0;
}
