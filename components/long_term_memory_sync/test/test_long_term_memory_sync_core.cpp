#include "long_term_memory_sync_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

#define CORE_CAPACITY LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY

const char *const KEYS[CORE_CAPACITY] = {
    "favorite_color",
    "pet_name",
    nullptr,
};
const char *const VALUES[CORE_CAPACITY] = {
    "蓝色",
    "布丁",
    nullptr,
};

void test_state_reset() {
    long_term_memory_sync_state_t state = {};
    state.revision = 12;
    state.has_snapshot = true;
    long_term_memory_sync_state_reset(&state);
    assert(state.revision == 0);
    assert(!state.has_snapshot);
    long_term_memory_sync_state_reset(nullptr);
}

void test_holds_key_matches_child_and_key() {
    long_term_memory_entry_t entries[2] = {};
    std::strncpy(entries[0].child_id, "child_001", 39);
    std::strncpy(entries[0].key, "favorite_color", 31);
    std::strncpy(entries[1].child_id, "child_002", 39);
    std::strncpy(entries[1].key, "pet_name", 31);

    assert(
        long_term_memory_sync_holds_key(
            entries,
            2,
            "child_001",
            "favorite_color"
        )
    );
    assert(
        long_term_memory_sync_holds_key(entries, 2, "child_002", "pet_name")
    );
    // A key stored for another child must not mask this child's key.
    assert(
        !long_term_memory_sync_holds_key(
            entries,
            2,
            "child_002",
            "favorite_color"
        )
    );
    assert(!long_term_memory_sync_holds_key(nullptr, 0, "a", "b"));
    assert(!long_term_memory_sync_holds_key(entries, 2, nullptr, "b"));
}

void test_child_id_is_device_scoped() {
    char child_id[LONG_TERM_MEMORY_CHILD_ID_SIZE] = {};
    assert(
        long_term_memory_sync_child_id(child_id, sizeof(child_id), "device-1")
    );
    assert(std::strcmp(child_id, "sync:device-1") == 0);

    // A device id that cannot fit the local child field is refused instead of
    // being truncated into a different child.
    char tiny[8] = {};
    assert(!long_term_memory_sync_child_id(tiny, sizeof(tiny), "device-1"));
    assert(tiny[0] == '\0');
    assert(!long_term_memory_sync_child_id(child_id, 0, "device-1"));
    assert(!long_term_memory_sync_child_id(child_id, 40, ""));
}

void test_parse_accepts_well_formed_snapshot() {
    const char *keys[] = {KEYS[0], KEYS[1]};
    const char *values[] = {VALUES[0], VALUES[1]};
    const int32_t confidences[] = {65536, 60000};
    const int64_t revisions[] = {4, 5};
    const int64_t updated_at[] = {1700000100, 1700000200};

    long_term_memory_sync_remote_t remote[CORE_CAPACITY] = {};
    size_t count = 0;
    assert(long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        2,
        keys,
        values,
        confidences,
        revisions,
        updated_at,
        5,
        0,
        false
    ));
    assert(count == 2);
    assert(std::strcmp(remote[0].memory_key, "favorite_color") == 0);
    assert(std::strcmp(remote[0].memory_value, "蓝色") == 0);
    assert(remote[0].confidence == 65536);
    assert(remote[0].revision == 4);
    assert(remote[0].updated_at_epoch_seconds == 1700000100);
    assert(std::strcmp(remote[1].memory_key, "pet_name") == 0);
}

void test_parse_drops_duplicate_keys() {
    const char *keys[] = {KEYS[0], KEYS[0]};
    const char *values[] = {"蓝色", "绿色"};
    const int32_t confidences[] = {65536, 65536};
    const int64_t revisions[] = {1, 2};
    const int64_t updated_at[] = {1, 2};

    long_term_memory_sync_remote_t remote[CORE_CAPACITY] = {};
    size_t count = 0;
    assert(long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        2,
        keys,
        values,
        confidences,
        revisions,
        updated_at,
        2,
        0,
        false
    ));
    assert(count == 1);
    assert(std::strcmp(remote[0].memory_value, "蓝色") == 0);
}

void test_parse_rejects_unusable_entries() {
    const char *keys[] = {"", KEYS[0]};
    const char *values[] = {"蓝色", "蓝色"};
    const int32_t confidences[] = {65536, 65536};
    const int64_t revisions[] = {1, 1};
    const int64_t updated_at[] = {1, 1};

    long_term_memory_sync_remote_t remote[CORE_CAPACITY] = {};
    size_t count = 99;
    // A non-empty snapshot whose entries all fail validation must be rejected;
    // otherwise it would look like an intentional empty snapshot.
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        1,
        keys,
        values,
        confidences,
        revisions,
        updated_at,
        1,
        0,
        false
    ));
    assert(count == 0);

    const char *long_key[] = {
        "a_key_that_is_far_too_long_for_the_local_store_field_0123456789"
    };
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        1,
        long_key,
        values,
        confidences,
        revisions,
        updated_at,
        1,
        0,
        false
    ));

    const int32_t bad_confidence[] = {70000};
    const char *one_key[] = {KEYS[0]};
    const char *one_value[] = {VALUES[0]};
    const int64_t one_revision[] = {1};
    const int64_t one_updated_at[] = {1};
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        1,
        one_key,
        one_value,
        bad_confidence,
        one_revision,
        one_updated_at,
        1,
        0,
        false
    ));

    const int32_t negative_confidence[] = {-1};
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        1,
        one_key,
        one_value,
        negative_confidence,
        one_revision,
        one_updated_at,
        1,
        0,
        false
    ));
}

void test_parse_accepts_empty_snapshot() {
    long_term_memory_sync_remote_t remote[CORE_CAPACITY] = {};
    size_t count = 99;
    assert(long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        6,
        0,
        false
    ));
    assert(count == 0);
}

void test_parse_rejects_oversized_snapshot() {
    const char *keys[CORE_CAPACITY + 1] = {};
    const char *values[CORE_CAPACITY + 1] = {};
    int32_t confidences[CORE_CAPACITY + 1] = {};
    int64_t revisions[CORE_CAPACITY + 1] = {};
    int64_t updated_at[CORE_CAPACITY + 1] = {};
    for (size_t index = 0; index < CORE_CAPACITY + 1; ++index) {
        keys[index] = "favorite_color";
        values[index] = "蓝色";
        confidences[index] = 65536;
        revisions[index] = 1;
        updated_at[index] = 1;
    }
    long_term_memory_sync_remote_t remote[CORE_CAPACITY] = {};
    size_t count = 0;
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        CORE_CAPACITY + 1,
        keys,
        values,
        confidences,
        revisions,
        updated_at,
        1,
        0,
        false
    ));
}

void test_parse_rejects_rewinding_revision() {
    const char *keys[] = {KEYS[0]};
    const char *values[] = {VALUES[0]};
    const int32_t confidences[] = {65536};
    const int64_t revisions[] = {1};
    const int64_t updated_at[] = {1};
    long_term_memory_sync_remote_t remote[CORE_CAPACITY] = {};
    size_t count = 0;

    // Once a snapshot has been applied, an older revision may not win: the
    // server may have deleted facts the device would resurrect.
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        1,
        keys,
        values,
        confidences,
        revisions,
        updated_at,
        3,
        7,
        true
    ));
    // Equal revisions are a normal re-delivery of the same window.
    assert(long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        1,
        keys,
        values,
        confidences,
        revisions,
        updated_at,
        7,
        7,
        true
    ));
    assert(count == 1);
    // A negative revision is never trusted as a cursor.
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        -1,
        0,
        false
    ));
}

void test_parse_rejects_mismatched_arrays() {
    long_term_memory_sync_remote_t remote[CORE_CAPACITY] = {};
    size_t count = 0;
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        &count,
        1,
        nullptr,
        VALUES,
        nullptr,
        nullptr,
        nullptr,
        1,
        0,
        false
    ));
    assert(!long_term_memory_sync_parse_remote(
        remote,
        CORE_CAPACITY,
        nullptr,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        1,
        0,
        false
    ));
    assert(!long_term_memory_sync_parse_remote(
        nullptr,
        4,
        &count,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        1,
        0,
        false
    ));
}

void test_pending_add_dedupes_and_bounds() {
    char pending[LONG_TERM_MEMORY_SYNC_PENDING_CAPACITY]
                [LONG_TERM_MEMORY_KEY_SIZE] = {};
    size_t count = 0;
    assert(long_term_memory_sync_pending_add(
        pending,
        LONG_TERM_MEMORY_SYNC_PENDING_CAPACITY,
        &count,
        "favorite_color"
    ));
    assert(count == 1);
    // Re-enqueueing the same key is success but does not grow the queue.
    assert(long_term_memory_sync_pending_add(
        pending,
        LONG_TERM_MEMORY_SYNC_PENDING_CAPACITY,
        &count,
        "favorite_color"
    ));
    assert(count == 1);
    // The queue is a contiguous array of fixed-width rows, so a presence check
    // needs a real pointer table, exactly as production code builds one.
    const char *pending_pointers[1] = {pending[0]};
    assert(long_term_memory_sync_pending_contains(
        pending_pointers,
        count,
        "favorite_color"
    ));
    assert(!long_term_memory_sync_pending_contains(
        pending_pointers,
        count,
        "pet_name"
    ));

    char tiny[2][LONG_TERM_MEMORY_KEY_SIZE] = {};
    size_t tiny_count = 0;
    assert(long_term_memory_sync_pending_add(
        tiny,
        2,
        &tiny_count,
        "favorite_color"
    ));
    assert(long_term_memory_sync_pending_add(
        tiny,
        2,
        &tiny_count,
        "pet_name"
    ));
    assert(tiny_count == 2);
    assert(!long_term_memory_sync_pending_add(
        tiny,
        2,
        &tiny_count,
        "favorite_food"
    ));
    assert(tiny_count == 2);
}

void test_pending_add_rejects_unusable_keys() {
    char pending[4][LONG_TERM_MEMORY_KEY_SIZE] = {};
    size_t count = 0;
    assert(!long_term_memory_sync_pending_add(pending, 4, &count, nullptr));
    assert(!long_term_memory_sync_pending_add(pending, 4, &count, ""));
    assert(
        !long_term_memory_sync_pending_add(
            pending,
            4,
            &count,
            "a_key_that_is_far_too_long_for_the_local_store_field_0123456789"
        )
    );
    assert(!long_term_memory_sync_pending_add(nullptr, 4, &count, "key"));
    assert(!long_term_memory_sync_pending_add(pending, 4, nullptr, "key"));
    assert(count == 0);
}

}  // namespace

int main() {
    test_state_reset();
    test_holds_key_matches_child_and_key();
    test_child_id_is_device_scoped();
    test_parse_accepts_well_formed_snapshot();
    test_parse_drops_duplicate_keys();
    test_parse_rejects_unusable_entries();
    test_parse_accepts_empty_snapshot();
    test_parse_rejects_oversized_snapshot();
    test_parse_rejects_rewinding_revision();
    test_parse_rejects_mismatched_arrays();
    test_pending_add_dedupes_and_bounds();
    test_pending_add_rejects_unusable_keys();
    std::printf("long_term_memory_sync_core: all checks passed\n");
    return 0;
}
