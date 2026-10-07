#include "content_library_index.h"

#include <cassert>
#include <cstring>

static content_library_entry_t make_entry(
    const char *package_id,
    uint32_t version,
    int64_t published_at,
    content_library_state_t state
) {
    content_library_entry_t entry = {};
    std::strncpy(entry.package_id, package_id, sizeof(entry.package_id) - 1);
    entry.package_version = version;
    std::strncpy(entry.title, "测试内容", sizeof(entry.title) - 1);
    std::strncpy(entry.category, "story", sizeof(entry.category) - 1);
    entry.age_tiers[1] = true;
    std::strncpy(entry.asset_key, "story/demo", sizeof(entry.asset_key) - 1);
    std::memset(entry.sha256, 'a', 64);
    entry.sha256[64] = '\0';
    entry.size_bytes = 1024;
    entry.published_at = published_at;
    std::strncpy(
        entry.local_path,
        "/content/story_demo.pkg",
        sizeof(entry.local_path) - 1
    );
    entry.state = state;
    return entry;
}

int main() {
    content_library_entry_t storage[4] = {};
    content_index_t index = {};
    assert(content_index_init(&index, storage, 4) == CONTENT_INDEX_OK);

    content_library_manifest_item_t first = {};
    first.operation = CONTENT_LIBRARY_OPERATION_UPSERT;
    first.entry = make_entry(
        "story_demo",
        1,
        10,
        CONTENT_LIBRARY_STATE_DOWNLOADING
    );
    bool changed = false;
    assert(
        content_index_apply_item(&index, &first, &changed) == CONTENT_INDEX_OK
    );
    assert(changed);
    assert(index.count == 1);

    first.entry.package_version = 2;
    first.entry.published_at = 20;
    first.entry.state = CONTENT_LIBRARY_STATE_READY;
    assert(
        content_index_apply_item(&index, &first, &changed) == CONTENT_INDEX_OK
    );
    assert(changed);
    assert(index.entries[0].package_version == 2);
    assert(index.entries[0].state == CONTENT_LIBRARY_STATE_READY);

    content_library_manifest_item_t refreshed = first;
    refreshed.entry = make_entry(
        "story_demo",
        2,
        20,
        CONTENT_LIBRARY_STATE_MISSING
    );
    refreshed.entry.local_path[0] = '\0';
    assert(
        content_index_apply_item(&index, &refreshed, &changed) == CONTENT_INDEX_OK
    );
    assert(index.entries[0].state == CONTENT_LIBRARY_STATE_READY);
    assert(std::strcmp(index.entries[0].local_path, "/content/story_demo.pkg") == 0);

    first.entry.package_version = 1;
    assert(
        content_index_apply_item(&index, &first, &changed) == CONTENT_INDEX_OK
    );
    assert(!changed);
    assert(index.entries[0].package_version == 2);

    content_library_manifest_item_t second = {};
    second.operation = CONTENT_LIBRARY_OPERATION_UPSERT;
    second.entry = make_entry(
        "story_other",
        1,
        5,
        CONTENT_LIBRARY_STATE_READY
    );
    assert(
        content_index_apply_item(&index, &second, &changed) == CONTENT_INDEX_OK
    );
    assert(index.count == 2);

    second.operation = CONTENT_LIBRARY_OPERATION_WITHDRAW;
    std::strncpy(
        second.entry.package_id,
        "story_demo",
        sizeof(second.entry.package_id) - 1
    );
    assert(
        content_index_apply_item(&index, &second, &changed) == CONTENT_INDEX_OK
    );
    assert(changed);
    assert(index.count == 1);
    assert(std::strcmp(index.entries[0].package_id, "story_other") == 0);

    content_library_entry_t oldest = {};
    assert(content_index_oldest(&index, &oldest) == CONTENT_INDEX_OK);
    assert(std::strcmp(oldest.package_id, "story_other") == 0);

    second.operation = CONTENT_LIBRARY_OPERATION_WITHDRAW;
    std::strncpy(
        second.entry.package_id,
        "story_missing",
        sizeof(second.entry.package_id) - 1
    );
    assert(
        content_index_apply_item(&index, &second, &changed) == CONTENT_INDEX_OK
    );
    assert(!changed);
    assert(index.count == 1);

    return 0;
}
