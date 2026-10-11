#include "theme_package_core.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static theme_pack_t make_pack(
    const char *theme_id,
    const char *title,
    int member_count
) {
    theme_pack_t pack = {};
    std::strncpy(pack.theme_id, theme_id, CONTENT_LIBRARY_ID_SIZE - 1);
    std::strncpy(pack.title, title, THEME_PACKAGE_TITLE_SIZE - 1);
    pack.member_count = static_cast<size_t>(member_count);
    for (int index = 0; index < member_count; ++index) {
        char id[CONTENT_LIBRARY_ID_SIZE];
        std::snprintf(id, sizeof(id), "content_story_%03d", index);
        std::strncpy(
            pack.package_ids[index],
            id,
            CONTENT_LIBRARY_ID_SIZE - 1
        );
    }
    return pack;
}

int main() {
    theme_package_core_t core = {};
    theme_package_core_init(&core);
    assert(core.count == 0);

    const theme_pack_t ocean = make_pack("theme_ocean", "海洋主题", 3);
    assert(theme_package_core_upsert(&core, &ocean) == THEME_PACKAGE_OK);
    assert(core.count == 1);
    assert(std::strcmp(core.packs[0].theme_id, "theme_ocean") == 0);

    const theme_pack_t night = make_pack("theme_night", "睡前一周", 2);
    assert(theme_package_core_upsert(&core, &night) == THEME_PACKAGE_OK);
    assert(core.count == 2);
    // Newest change leads.
    assert(std::strcmp(core.packs[0].theme_id, "theme_night") == 0);

    // Re-upserting an existing pack replaces it and moves it to the head.
    theme_pack_t ocean_v2 = make_pack("theme_ocean", "海洋主题 v2", 4);
    assert(theme_package_core_upsert(&core, &ocean_v2) == THEME_PACKAGE_OK);
    assert(core.count == 2);
    assert(std::strcmp(core.packs[0].theme_id, "theme_ocean") == 0);
    assert(std::strcmp(core.packs[0].title, "海洋主题 v2") == 0);
    assert(core.packs[0].member_count == 4);

    // Find reports the index, or the count when absent.
    assert(theme_package_core_find(&core, "theme_ocean") == 0);
    assert(theme_package_core_find(&core, "theme_night") == 1);
    assert(theme_package_core_find(&core, "theme_missing") == core.count);

    // Invalid packs are rejected.
    theme_pack_t invalid = make_pack("", "空编号", 1);
    assert(
        theme_package_core_upsert(&core, &invalid) ==
        THEME_PACKAGE_ERR_INVALID_ARGUMENT
    );
    invalid = make_pack("theme_ocean", "", 1);
    assert(
        theme_package_core_upsert(&core, &invalid) ==
        THEME_PACKAGE_ERR_INVALID_ARGUMENT
    );
    invalid = make_pack("theme_ocean", "无成员", 0);
    assert(
        theme_package_core_upsert(&core, &invalid) ==
        THEME_PACKAGE_ERR_INVALID_ARGUMENT
    );

    // Removing a missing pack is success.
    assert(
        theme_package_core_remove(&core, "theme_missing") == THEME_PACKAGE_OK
    );
    assert(core.count == 2);

    // Removing an existing pack drops exactly one entry.
    assert(theme_package_core_remove(&core, "theme_night") == THEME_PACKAGE_OK);
    assert(core.count == 1);
    assert(theme_package_core_find(&core, "theme_night") == core.count);

    // Capacity is bounded: inserting past the limit reports FULL.
    theme_package_core_t full = {};
    theme_package_core_init(&full);
    char id[CONTENT_LIBRARY_ID_SIZE];
    for (int index = 0; index < THEME_PACKAGE_CAPACITY; ++index) {
        std::snprintf(id, sizeof(id), "theme_pack_%03d", index);
        const theme_pack_t pack = make_pack(id, "主题", 1);
        assert(theme_package_core_upsert(&full, &pack) == THEME_PACKAGE_OK);
    }
    assert(full.count == THEME_PACKAGE_CAPACITY);
    const theme_pack_t overflow = make_pack("theme_overflow", "溢出", 1);
    assert(
        theme_package_core_upsert(&full, &overflow) == THEME_PACKAGE_ERR_FULL
    );
    // Removing one frees exactly one slot.
    assert(theme_package_core_remove(&full, "theme_pack_000") == THEME_PACKAGE_OK);
    assert(theme_package_core_upsert(&full, &overflow) == THEME_PACKAGE_OK);
    assert(full.count == THEME_PACKAGE_CAPACITY);

    return 0;
}
