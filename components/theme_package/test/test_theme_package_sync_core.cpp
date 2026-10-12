#include "theme_package_sync_core.h"

#include <cassert>
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
    const theme_pack_t local = make_pack("theme_local", "本地主题", 1);
    assert(theme_package_core_upsert(&core, &local) == THEME_PACKAGE_OK);

    theme_pack_t remote[] = {
        make_pack("theme_ocean", "海洋主题", 3),
        make_pack("theme_night", "睡前一周", 2),
        make_pack("theme_ocean", "海洋主题 重复", 1),
    };
    remote[0].catalog_revision = 7;
    assert(
        theme_package_sync_core_replace(&core, remote, 3) ==
        THEME_PACKAGE_OK
    );
    assert(core.count == 2);
    assert(std::strcmp(core.packs[0].theme_id, "theme_ocean") == 0);
    assert(core.packs[0].catalog_revision == 7);
    assert(std::strcmp(core.packs[1].theme_id, "theme_night") == 0);
    assert(theme_package_core_find(&core, "theme_local") == core.count);

    // An empty valid remote snapshot is authoritative and clears the cache.
    assert(theme_package_sync_core_replace(&core, nullptr, 0) == THEME_PACKAGE_OK);
    assert(core.count == 0);

    // Invalid packs are dropped; a snapshot with only invalid packs is
    // rejected without disturbing the local cache.
    const theme_pack_t ocean = make_pack("theme_ocean", "海洋主题", 2);
    assert(theme_package_core_upsert(&core, &ocean) == THEME_PACKAGE_OK);
    theme_pack_t partial[] = {
        make_pack("", "无编号", 1),
        make_pack("theme_ocean", "海洋主题", 1),
    };
    assert(
        theme_package_sync_core_replace(&core, partial, 2) ==
        THEME_PACKAGE_OK
    );
    assert(core.count == 1);
    assert(std::strcmp(core.packs[0].theme_id, "theme_ocean") == 0);

    theme_pack_t invalid_only[] = {
        make_pack("theme_nomembers", "缺少成员", 0),
        make_pack("theme_notitle", "", 1),
    };
    assert(
        theme_package_sync_core_replace(&core, invalid_only, 2) ==
        THEME_PACKAGE_ERR_INVALID_ARGUMENT
    );
    assert(core.count == 1);
    assert(std::strcmp(core.packs[0].theme_id, "theme_ocean") == 0);

    // Capacity is bounded without rejecting the whole snapshot.
    theme_pack_t too_many[THEME_PACKAGE_CAPACITY + 2] = {};
    char ids[THEME_PACKAGE_CAPACITY + 2][CONTENT_LIBRARY_ID_SIZE] = {};
    for (int index = 0; index < THEME_PACKAGE_CAPACITY + 2; ++index) {
        std::snprintf(ids[index], sizeof(ids[index]), "theme_pack_%03d", index);
        too_many[index] = make_pack(ids[index], "主题", 1);
    }
    assert(
        theme_package_sync_core_replace(
            &core,
            too_many,
            THEME_PACKAGE_CAPACITY + 2
        ) == THEME_PACKAGE_OK
    );
    assert(core.count == THEME_PACKAGE_CAPACITY);
    assert(std::strcmp(core.packs[0].theme_id, "theme_pack_000") == 0);

    return 0;
}
