#include "theme_package_core.h"

#include <string.h>

void theme_package_core_init(theme_package_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

static bool theme_pack_is_valid(const theme_pack_t *pack) {
    if (pack == NULL) {
        return false;
    }
    if (pack->theme_id[0] == '\0' || pack->title[0] == '\0') {
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

size_t theme_package_core_find(
    const theme_package_core_t *core,
    const char *theme_id
) {
    if (core == NULL || theme_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->count; ++index) {
        if (strcmp(core->packs[index].theme_id, theme_id) == 0) {
            return index;
        }
    }
    return core->count;
}

theme_package_error_t theme_package_core_upsert(
    theme_package_core_t *core,
    const theme_pack_t *pack
) {
    if (core == NULL) {
        return THEME_PACKAGE_ERR_NOT_INITIALIZED;
    }
    if (!theme_pack_is_valid(pack)) {
        return THEME_PACKAGE_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = theme_package_core_find(core, pack->theme_id);
    if (existing >= core->count && core->count >= THEME_PACKAGE_CAPACITY) {
        return THEME_PACKAGE_ERR_FULL;
    }
    if (existing < core->count) {
        // Replace in place: shift entries above it down one slot, then write
        // the rebuilt pack at the head. The count is unchanged.
        for (size_t index = existing; index > 0; --index) {
            core->packs[index] = core->packs[index - 1];
        }
        core->packs[0] = *pack;
        return THEME_PACKAGE_OK;
    }
    // Insert a new pack: shift every entry down, dropping the oldest when the
    // cache is already full.
    const size_t last = core->count < THEME_PACKAGE_CAPACITY
        ? core->count
        : THEME_PACKAGE_CAPACITY - 1;
    for (size_t index = last; index > 0; --index) {
        core->packs[index] = core->packs[index - 1];
    }
    core->packs[0] = *pack;
    core->count += 1;
    return THEME_PACKAGE_OK;
}

theme_package_error_t theme_package_core_remove(
    theme_package_core_t *core,
    const char *theme_id
) {
    if (core == NULL) {
        return THEME_PACKAGE_ERR_NOT_INITIALIZED;
    }
    if (theme_id == NULL || theme_id[0] == '\0') {
        return THEME_PACKAGE_ERR_INVALID_ARGUMENT;
    }
    const size_t index = theme_package_core_find(core, theme_id);
    if (index >= core->count) {
        return THEME_PACKAGE_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->count; ++cursor) {
        core->packs[cursor] = core->packs[cursor + 1];
    }
    core->count -= 1;
    memset(&core->packs[core->count], 0, sizeof(theme_pack_t));
    return THEME_PACKAGE_OK;
}
