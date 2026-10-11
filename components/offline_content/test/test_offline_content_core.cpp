#include "offline_content_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static offline_content_entry_t make_entry(
    const char *package_id,
    const char *title,
    bool ready
) {
    offline_content_entry_t entry;
    std::memset(&entry, 0, sizeof(entry));
    std::strncpy(
        entry.package_id,
        package_id,
        OFFLINE_CONTENT_ID_SIZE - 1
    );
    std::strncpy(entry.title, title, OFFLINE_CONTENT_TITLE_SIZE - 1);
    std::strncpy(entry.category, "story", OFFLINE_CONTENT_CATEGORY_SIZE - 1);
    entry.package_version = 1;
    entry.size_bytes = 4096;
    entry.local_ready = ready;
    entry.pinned_at_epoch_seconds = 1700000000;
    return entry;
}

int main() {
    offline_content_core_t core;
    offline_content_core_init(&core);
    assert(core.count == 0);

    const offline_content_entry_t first = make_entry("story_001", "种子", false);
    assert(offline_content_core_pin(&core, &first) == OFFLINE_CONTENT_OK);
    assert(core.count == 1);
    const offline_content_entry_t second = make_entry("song_002", "儿歌", false);
    assert(offline_content_core_pin(&core, &second) == OFFLINE_CONTENT_OK);
    assert(core.count == 2);
    // Newest pinned leads.
    assert(std::strcmp(core.entries[0].package_id, "song_002") == 0);

    // Nothing is playable until bytes are verified.
    offline_content_entry_t playable;
    std::memset(&playable, 0, sizeof(playable));
    assert(!offline_content_core_next_playable(&core, &playable));
    assert(
        offline_content_core_mark_ready(&core, "story_001") ==
        OFFLINE_CONTENT_OK
    );
    assert(offline_content_core_next_playable(&core, &playable));
    assert(std::strcmp(playable.package_id, "story_001") == 0);

    // Marking an unknown package reports NOT_FOUND.
    assert(
        offline_content_core_mark_ready(&core, "story_missing") ==
        OFFLINE_CONTENT_ERR_NOT_FOUND
    );

    // Re-pinning replaces and moves to the head.
    const offline_content_entry_t first_v2 = make_entry(
        "story_001",
        "种子（新版）",
        false
    );
    assert(offline_content_core_pin(&core, &first_v2) == OFFLINE_CONTENT_OK);
    assert(core.count == 2);
    assert(std::strcmp(core.entries[0].package_id, "story_001") == 0);
    assert(offline_content_core_find(&core, "story_001") == 0);
    assert(offline_content_core_find(&core, "song_002") == 1);
    assert(offline_content_core_find(&core, "missing") == core.count);

    // Invalid entries are rejected.
    offline_content_entry_t invalid = make_entry("", "无 id", true);
    assert(
        offline_content_core_pin(&core, &invalid) ==
        OFFLINE_CONTENT_ERR_INVALID_ARGUMENT
    );
    invalid = make_entry("story_003", "无版本", true);
    invalid.package_version = 0;
    assert(
        offline_content_core_pin(&core, &invalid) ==
        OFFLINE_CONTENT_ERR_INVALID_ARGUMENT
    );

    // Unpinning a missing package is success.
    assert(
        offline_content_core_unpin(&core, "missing") == OFFLINE_CONTENT_OK
    );
    assert(offline_content_core_unpin(&core, "song_002") == OFFLINE_CONTENT_OK);
    assert(core.count == 1);

    // Capacity is bounded.
    offline_content_core_t full;
    offline_content_core_init(&full);
    char id[OFFLINE_CONTENT_ID_SIZE];
    for (int index = 0; index < OFFLINE_CONTENT_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "pkg_%03d", index);
        const offline_content_entry_t entry = make_entry(id, "内容", true);
        assert(offline_content_core_pin(&full, &entry) == OFFLINE_CONTENT_OK);
    }
    assert(full.count == OFFLINE_CONTENT_CAPACITY);
    const offline_content_entry_t overflow = make_entry("pkg_overflow", "溢出", true);
    assert(
        offline_content_core_pin(&full, &overflow) == OFFLINE_CONTENT_ERR_FULL
    );

    // A null store reports NOT_INITIALIZED.
    assert(
        offline_content_core_pin(nullptr, &first) ==
        OFFLINE_CONTENT_ERR_NOT_INITIALIZED
    );

    return 0;
}
