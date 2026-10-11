#include "favorites_core.h"

#include <string.h>

static bool favorites_id_is_valid(const char *package_id) {
    if (package_id == NULL || package_id[0] == '\0') {
        return false;
    }
    return strlen(package_id) < CONTENT_LIBRARY_ID_SIZE;
}

static size_t favorites_find(
    const favorites_core_t *core,
    const char *package_id
) {
    for (size_t index = 0; index < core->count; ++index) {
        if (strcmp(core->package_ids[index], package_id) == 0) {
            return index;
        }
    }
    return core->count;
}

void favorites_core_init(favorites_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

favorites_error_t favorites_core_add(
    favorites_core_t *core,
    const char *package_id
) {
    if (core == NULL) {
        return FAVORITES_ERR_NOT_INITIALIZED;
    }
    if (!favorites_id_is_valid(package_id)) {
        return FAVORITES_ERR_INVALID_ARGUMENT;
    }
    // A repeat add is idempotent: the existing bookmark stays where it is.
    if (favorites_find(core, package_id) < core->count) {
        return FAVORITES_OK;
    }
    if (core->count >= FAVORITES_CAPACITY) {
        return FAVORITES_ERR_FULL;
    }
    // Newest-first: shift every entry down one slot before writing the head.
    for (size_t index = core->count; index > 0; --index) {
        memcpy(
            core->package_ids[index],
            core->package_ids[index - 1],
            CONTENT_LIBRARY_ID_SIZE
        );
    }
    strncpy(
        core->package_ids[0],
        package_id,
        CONTENT_LIBRARY_ID_SIZE - 1
    );
    core->package_ids[0][CONTENT_LIBRARY_ID_SIZE - 1] = '\0';
    core->count += 1;
    return FAVORITES_OK;
}

favorites_error_t favorites_core_remove(
    favorites_core_t *core,
    const char *package_id
) {
    if (core == NULL) {
        return FAVORITES_ERR_NOT_INITIALIZED;
    }
    if (!favorites_id_is_valid(package_id)) {
        return FAVORITES_ERR_INVALID_ARGUMENT;
    }
    const size_t index = favorites_find(core, package_id);
    if (index >= core->count) {
        return FAVORITES_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->count; ++cursor) {
        memcpy(
            core->package_ids[cursor],
            core->package_ids[cursor + 1],
            CONTENT_LIBRARY_ID_SIZE
        );
    }
    core->count -= 1;
    memset(
        core->package_ids[core->count],
        0,
        CONTENT_LIBRARY_ID_SIZE
    );
    return FAVORITES_OK;
}

favorites_error_t favorites_core_toggle(
    favorites_core_t *core,
    const char *package_id,
    bool *is_favorite_out
) {
    if (core == NULL) {
        return FAVORITES_ERR_NOT_INITIALIZED;
    }
    if (!favorites_id_is_valid(package_id)) {
        return FAVORITES_ERR_INVALID_ARGUMENT;
    }
    if (favorites_find(core, package_id) < core->count) {
        const favorites_error_t removed = favorites_core_remove(core, package_id);
        if (removed != FAVORITES_OK) {
            return removed;
        }
        if (is_favorite_out != NULL) {
            *is_favorite_out = false;
        }
        return FAVORITES_OK;
    }
    const favorites_error_t added = favorites_core_add(core, package_id);
    if (added != FAVORITES_OK) {
        return added;
    }
    if (is_favorite_out != NULL) {
        *is_favorite_out = true;
    }
    return FAVORITES_OK;
}

bool favorites_core_contains(
    const favorites_core_t *core,
    const char *package_id
) {
    if (core == NULL || !favorites_id_is_valid(package_id)) {
        return false;
    }
    return favorites_find(core, package_id) < core->count;
}
