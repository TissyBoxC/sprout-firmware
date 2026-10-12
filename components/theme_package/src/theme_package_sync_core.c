#include "theme_package_sync_core.h"

#include <string.h>

static bool theme_package_sync_pack_is_valid(const theme_pack_t *pack) {
    if (pack == NULL || pack->theme_id[0] == '\0' || pack->title[0] == '\0') {
        return false;
    }
    if (pack->member_count == 0 ||
        pack->member_count > THEME_PACKAGE_MEMBER_CAPACITY) {
        return false;
    }
    for (size_t index = 0; index < pack->member_count; ++index) {
        if (pack->package_ids[index][0] == '\0') {
            return false;
        }
    }
    return true;
}

static bool theme_package_sync_contains(
    const char seen_ids[][CONTENT_LIBRARY_ID_SIZE],
    size_t seen_count,
    const char *theme_id
) {
    for (size_t index = 0; index < seen_count; ++index) {
        if (strcmp(seen_ids[index], theme_id) == 0) {
            return true;
        }
    }
    return false;
}

theme_package_error_t theme_package_sync_core_replace(
    theme_package_core_t *core,
    const theme_pack_t *packs,
    size_t pack_count
) {
    if (core == NULL) {
        return THEME_PACKAGE_ERR_NOT_INITIALIZED;
    }
    if (packs == NULL && pack_count > 0) {
        return THEME_PACKAGE_ERR_INVALID_ARGUMENT;
    }
    if (pack_count == 0) {
        theme_package_core_init(core);
        return THEME_PACKAGE_OK;
    }

    // Track which packs were accepted so the second pass can copy them into
    // the cache without ever needing a second full-size cache on the stack.
    char seen_ids[THEME_PACKAGE_CAPACITY][CONTENT_LIBRARY_ID_SIZE] = {};
    size_t chosen[THEME_PACKAGE_CAPACITY] = {};
    size_t chosen_count = 0;
    for (size_t index = 0; index < pack_count; ++index) {
        const theme_pack_t *pack = &packs[index];
        if (!theme_package_sync_pack_is_valid(pack) ||
            theme_package_sync_contains(seen_ids, chosen_count, pack->theme_id)) {
            continue;
        }
        if (chosen_count >= THEME_PACKAGE_CAPACITY) {
            break;
        }
        memcpy(seen_ids[chosen_count], pack->theme_id, CONTENT_LIBRARY_ID_SIZE);
        chosen[chosen_count] = index;
        chosen_count += 1;
    }
    if (chosen_count == 0) {
        return THEME_PACKAGE_ERR_INVALID_ARGUMENT;
    }
    memset(core->packs, 0, sizeof(core->packs));
    for (size_t index = 0; index < chosen_count; ++index) {
        core->packs[index] = packs[chosen[index]];
    }
    core->count = chosen_count;
    return THEME_PACKAGE_OK;
}
