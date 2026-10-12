#include "favorites_sync_core.h"

#include <string.h>

static bool favorites_sync_id_is_valid(const char *package_id) {
    if (package_id == NULL || package_id[0] == '\0') {
        return false;
    }
    return strlen(package_id) < CONTENT_LIBRARY_ID_SIZE;
}

static bool favorites_sync_contains(
    const favorites_core_t *core,
    const char *package_id
) {
    for (size_t index = 0; index < core->count; ++index) {
        if (strcmp(core->package_ids[index], package_id) == 0) {
            return true;
        }
    }
    return false;
}

favorites_error_t favorites_sync_core_replace(
    favorites_core_t *core,
    const char *const *package_ids,
    size_t package_id_count
) {
    if (core == NULL) {
        return FAVORITES_ERR_NOT_INITIALIZED;
    }
    if (package_ids == NULL && package_id_count > 0) {
        return FAVORITES_ERR_INVALID_ARGUMENT;
    }
    if (package_id_count == 0) {
        favorites_core_init(core);
        return FAVORITES_OK;
    }

    favorites_core_t replacement;
    memset(&replacement, 0, sizeof(replacement));
    for (size_t index = 0; index < package_id_count; ++index) {
        const char *package_id = package_ids[index];
        if (!favorites_sync_id_is_valid(package_id) ||
            favorites_sync_contains(&replacement, package_id)) {
            continue;
        }
        if (replacement.count >= FAVORITES_CAPACITY) {
            break;
        }
        strncpy(
            replacement.package_ids[replacement.count],
            package_id,
            CONTENT_LIBRARY_ID_SIZE - 1
        );
        replacement.package_ids[replacement.count][
            CONTENT_LIBRARY_ID_SIZE - 1
        ] = '\0';
        replacement.count += 1;
    }
    if (replacement.count == 0) {
        return FAVORITES_ERR_INVALID_ARGUMENT;
    }
    *core = replacement;
    return FAVORITES_OK;
}
