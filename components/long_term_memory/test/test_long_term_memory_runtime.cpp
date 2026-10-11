#include "long_term_memory.h"

#include <cassert>
#include <cstring>

static long_term_memory_entry_t make_entry(
    const char *entry_id,
    const char *child_id,
    const char *key,
    const char *value
) {
    long_term_memory_entry_t entry = {};
    std::strncpy(entry.entry_id, entry_id, LONG_TERM_MEMORY_ID_SIZE - 1);
    std::strncpy(
        entry.child_id,
        child_id,
        LONG_TERM_MEMORY_CHILD_ID_SIZE - 1
    );
    std::strncpy(entry.key, key, LONG_TERM_MEMORY_KEY_SIZE - 1);
    std::strncpy(entry.value, value, LONG_TERM_MEMORY_VALUE_SIZE - 1);
    entry.updated_at_epoch_seconds = 1700000000;
    return entry;
}

int main() {
    assert(long_term_memory_init() == ESP_OK);
    assert(long_term_memory_is_ready());

    // Nothing is stored before the guardian authorizes memory.
    assert(!long_term_memory_is_authorized());
    const long_term_memory_entry_t favorite = make_entry(
        "entry_001",
        "child_001",
        "favorite_color",
        "蓝色"
    );
    assert(long_term_memory_remember(&favorite) == ESP_ERR_INVALID_STATE);
    long_term_memory_entry_t recalled = {};
    assert(!long_term_memory_recall("child_001", "favorite_color", &recalled));

    size_t listed = 99;
    assert(long_term_memory_list(nullptr, 0, &listed) == ESP_OK);
    assert(listed == 0);
    assert(long_term_memory_count() == 0);

    // Authorizing memory enables writes and reads.
    assert(long_term_memory_set_authorized(true) == ESP_OK);
    assert(long_term_memory_is_authorized());
    assert(long_term_memory_remember(&favorite) == ESP_OK);
    assert(long_term_memory_count() == 1);
    assert(long_term_memory_recall("child_001", "favorite_color", &recalled));
    assert(std::strcmp(recalled.value, "蓝色") == 0);

    long_term_memory_entry_t buffer[LONG_TERM_MEMORY_CAPACITY] = {};
    assert(
        long_term_memory_list(buffer, LONG_TERM_MEMORY_CAPACITY, &listed) ==
        ESP_OK
    );
    assert(listed == 1);
    assert(std::strcmp(buffer[0].key, "favorite_color") == 0);

    // The authorization flag and the entry survive a re-init (simulating a
    // restart against the same store).
    assert(long_term_memory_init() == ESP_OK);
    assert(long_term_memory_is_authorized());
    assert(long_term_memory_count() == 1);
    assert(long_term_memory_recall("child_001", "favorite_color", &recalled));

    // Revoking authorization erases every entry.
    assert(long_term_memory_set_authorized(false) == ESP_OK);
    assert(!long_term_memory_is_authorized());
    assert(long_term_memory_count() == 0);
    assert(!long_term_memory_recall("child_001", "favorite_color", &recalled));
    assert(long_term_memory_remember(&favorite) == ESP_ERR_INVALID_STATE);

    return 0;
}
