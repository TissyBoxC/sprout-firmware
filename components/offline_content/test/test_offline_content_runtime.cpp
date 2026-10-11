#include "offline_content.h"

#include <cassert>
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
    entry.package_version = 2;
    entry.size_bytes = 8192;
    entry.local_ready = ready;
    entry.pinned_at_epoch_seconds = 1700000000;
    return entry;
}

int main() {
    assert(offline_content_init() == ESP_OK);
    assert(offline_content_is_ready());
    assert(offline_content_count() == 0);

    const offline_content_entry_t first = make_entry("story_001", "种子", false);
    assert(offline_content_pin(&first) == ESP_OK);
    const offline_content_entry_t second = make_entry("song_002", "儿歌", false);
    assert(offline_content_pin(&second) == ESP_OK);
    assert(offline_content_count() == 2);

    // Nothing is playable until the library verifies the bytes.
    offline_content_entry_t playable;
    std::memset(&playable, 0, sizeof(playable));
    assert(!offline_content_next_playable(&playable));
    assert(offline_content_mark_ready("song_002") == ESP_OK);
    assert(offline_content_next_playable(&playable));
    assert(std::strcmp(playable.package_id, "song_002") == 0);
    assert(offline_content_mark_ready("song_missing") == ESP_ERR_NOT_FOUND);

    // get and list report the pinned entries in newest-first order.
    offline_content_entry_t recalled;
    std::memset(&recalled, 0, sizeof(recalled));
    assert(offline_content_get("story_001", &recalled));
    assert(std::strcmp(recalled.title, "种子") == 0);
    assert(!offline_content_get("missing", &recalled));

    offline_content_entry_t listed[OFFLINE_CONTENT_CAPACITY];
    size_t listed_count = 0;
    assert(
        offline_content_list(listed, OFFLINE_CONTENT_CAPACITY, &listed_count) ==
        ESP_OK
    );
    assert(listed_count == 2);
    assert(std::strcmp(listed[0].package_id, "song_002") == 0);

    offline_content_snapshot_t snapshot = offline_content_get_snapshot();
    assert(snapshot.pinned == 2);
    assert(snapshot.playable == 1);
    assert(std::strcmp(snapshot.last_package_id, "song_002") == 0);

    // The blob survives a re-init, standing in for a device restart.
    assert(offline_content_init() == ESP_OK);
    assert(offline_content_count() == 2);
    assert(offline_content_next_playable(&playable));
    assert(std::strcmp(playable.package_id, "song_002") == 0);

    // Unpin persists.
    assert(offline_content_unpin("story_001") == ESP_OK);
    assert(offline_content_count() == 1);
    assert(offline_content_init() == ESP_OK);
    assert(offline_content_count() == 1);
    assert(!offline_content_get("story_001", &recalled));

    // Count is reported even when the caller's buffer is smaller than the set.
    const offline_content_entry_t many[] = {
        make_entry("a", "一", true),
        make_entry("b", "二", true),
        make_entry("c", "三", true),
    };
    for (size_t index = 0; index < 3; ++index) {
        assert(offline_content_pin(&many[index]) == ESP_OK);
    }
    offline_content_entry_t one[1];
    size_t one_count = 0;
    assert(offline_content_list(one, 1, &one_count) == ESP_OK);
    assert(one_count == 4);
    assert(std::strcmp(one[0].package_id, "c") == 0);

    assert(
        std::strcmp(
            offline_content_error_name(OFFLINE_CONTENT_ERR_NOT_LOCAL),
            "not_local"
        ) == 0
    );

    return 0;
}
