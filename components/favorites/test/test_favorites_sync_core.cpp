#include "favorites_sync_core.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    favorites_core_t core = {};
    favorites_core_init(&core);
    assert(favorites_core_add(&core, "content_old") == FAVORITES_OK);

    const char *remote[] = {
        "content_story_002",
        "content_story_001",
        "content_story_002",
        "content_story_003",
    };
    assert(
        favorites_sync_core_replace(&core, remote, 4) == FAVORITES_OK
    );
    assert(core.count == 3);
    assert(std::strcmp(core.package_ids[0], "content_story_002") == 0);
    assert(std::strcmp(core.package_ids[1], "content_story_001") == 0);
    assert(std::strcmp(core.package_ids[2], "content_story_003") == 0);
    assert(!favorites_core_contains(&core, "content_old"));

    // An empty valid remote snapshot is authoritative and clears the list.
    assert(favorites_sync_core_replace(&core, nullptr, 0) == FAVORITES_OK);
    assert(core.count == 0);

    // Invalid ids are ignored; a snapshot with only invalid ids is rejected.
    assert(favorites_core_add(&core, "content_story_002") == FAVORITES_OK);
    const char *invalid_only[] = {"", "content_story_002"};
    assert(
        favorites_sync_core_replace(&core, invalid_only, 2) == FAVORITES_OK
    );
    assert(core.count == 1);
    assert(std::strcmp(core.package_ids[0], "content_story_002") == 0);

    const char *empty_ids[] = {"", ""};
    assert(
        favorites_sync_core_replace(&core, empty_ids, 2) ==
        FAVORITES_ERR_INVALID_ARGUMENT
    );
    assert(core.count == 1);

    // Capacity is bounded without rejecting the whole snapshot.
    const char *too_many[FAVORITES_CAPACITY + 2] = {};
    char ids[FAVORITES_CAPACITY + 2][CONTENT_LIBRARY_ID_SIZE] = {};
    for (int index = 0; index < FAVORITES_CAPACITY + 2; ++index) {
        std::snprintf(ids[index], sizeof(ids[index]), "content_%03d", index);
        too_many[index] = ids[index];
    }
    assert(
        favorites_sync_core_replace(
            &core,
            too_many,
            FAVORITES_CAPACITY + 2
        ) == FAVORITES_OK
    );
    assert(core.count == FAVORITES_CAPACITY);
    assert(std::strcmp(core.package_ids[0], "content_000") == 0);

    return 0;
}
