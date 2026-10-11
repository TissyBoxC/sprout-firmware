#include "favorites_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    favorites_core_t core = {};
    favorites_core_init(&core);
    assert(core.count == 0);

    assert(favorites_core_add(&core, "content_story_001") == FAVORITES_OK);
    assert(favorites_core_add(&core, "content_story_002") == FAVORITES_OK);
    assert(core.count == 2);
    // Newest-first ordering: the second add leads.
    assert(std::strcmp(core.package_ids[0], "content_story_002") == 0);
    assert(std::strcmp(core.package_ids[1], "content_story_001") == 0);

    // A repeat add is idempotent and does not duplicate or reorder.
    assert(favorites_core_add(&core, "content_story_001") == FAVORITES_OK);
    assert(core.count == 2);
    assert(std::strcmp(core.package_ids[1], "content_story_001") == 0);

    assert(favorites_core_contains(&core, "content_story_001"));
    assert(!favorites_core_contains(&core, "content_story_009"));

    // Toggle flips membership and reports the resulting state.
    bool is_favorite = false;
    assert(
        favorites_core_toggle(&core, "content_story_002", &is_favorite) ==
        FAVORITES_OK
    );
    assert(!is_favorite);
    assert(!favorites_core_contains(&core, "content_story_002"));
    assert(core.count == 1);

    assert(
        favorites_core_toggle(&core, "content_story_002", &is_favorite) ==
        FAVORITES_OK
    );
    assert(is_favorite);
    assert(favorites_core_contains(&core, "content_story_002"));
    assert(core.count == 2);

    // Removing an absent bookmark is success.
    assert(favorites_core_remove(&core, "content_missing_001") == FAVORITES_OK);
    assert(core.count == 2);

    // Empty and oversized ids are rejected.
    assert(
        favorites_core_add(&core, "") == FAVORITES_ERR_INVALID_ARGUMENT
    );
    char oversized[CONTENT_LIBRARY_ID_SIZE + 4];
    std::memset(oversized, 'a', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = '\0';
    assert(
        favorites_core_add(&core, oversized) ==
        FAVORITES_ERR_INVALID_ARGUMENT
    );

    // Capacity is bounded: filling past the limit reports FULL.
    favorites_core_t full = {};
    favorites_core_init(&full);
    char id[CONTENT_LIBRARY_ID_SIZE];
    for (int index = 0; index < FAVORITES_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "content_story_%03d", index);
        assert(favorites_core_add(&full, id) == FAVORITES_OK);
    }
    assert(full.count == FAVORITES_CAPACITY);
    assert(
        favorites_core_add(&full, "content_story_999") == FAVORITES_ERR_FULL
    );
    // Removing one frees exactly one slot.
    assert(favorites_core_remove(&full, "content_story_000") == FAVORITES_OK);
    assert(favorites_core_add(&full, "content_story_999") == FAVORITES_OK);
    assert(full.count == FAVORITES_CAPACITY);

    return 0;
}
