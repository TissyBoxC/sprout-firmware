#include "offline_content_core.h"

#include <string.h>

static bool offline_content_is_valid(const offline_content_entry_t *entry) {
    if (entry == NULL) {
        return false;
    }
    if (entry->package_id[0] == '\0') {
        return false;
    }
    if (entry->package_version == 0) {
        return false;
    }
    return true;
}

void offline_content_core_init(offline_content_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

size_t offline_content_core_find(
    const offline_content_core_t *core,
    const char *package_id
) {
    if (core == NULL || package_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->count; ++index) {
        if (strcmp(core->entries[index].package_id, package_id) == 0) {
            return index;
        }
    }
    return core->count;
}

offline_content_error_t offline_content_core_pin(
    offline_content_core_t *core,
    const offline_content_entry_t *entry
) {
    if (core == NULL) {
        return OFFLINE_CONTENT_ERR_NOT_INITIALIZED;
    }
    if (!offline_content_is_valid(entry)) {
        return OFFLINE_CONTENT_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = offline_content_core_find(
        core,
        entry->package_id
    );
    if (existing >= core->count &&
        core->count >= OFFLINE_CONTENT_CAPACITY) {
        return OFFLINE_CONTENT_ERR_FULL;
    }
    if (existing < core->count) {
        for (size_t index = existing; index > 0; --index) {
            core->entries[index] = core->entries[index - 1];
        }
        core->entries[0] = *entry;
        return OFFLINE_CONTENT_OK;
    }
    const size_t last = core->count < OFFLINE_CONTENT_CAPACITY
        ? core->count
        : OFFLINE_CONTENT_CAPACITY - 1;
    for (size_t index = last; index > 0; --index) {
        core->entries[index] = core->entries[index - 1];
    }
    core->entries[0] = *entry;
    core->count += 1;
    return OFFLINE_CONTENT_OK;
}

offline_content_error_t offline_content_core_mark_ready(
    offline_content_core_t *core,
    const char *package_id
) {
    if (core == NULL) {
        return OFFLINE_CONTENT_ERR_NOT_INITIALIZED;
    }
    if (package_id == NULL || package_id[0] == '\0') {
        return OFFLINE_CONTENT_ERR_INVALID_ARGUMENT;
    }
    const size_t index = offline_content_core_find(core, package_id);
    if (index >= core->count) {
        return OFFLINE_CONTENT_ERR_NOT_FOUND;
    }
    core->entries[index].local_ready = true;
    return OFFLINE_CONTENT_OK;
}

offline_content_error_t offline_content_core_unpin(
    offline_content_core_t *core,
    const char *package_id
) {
    if (core == NULL) {
        return OFFLINE_CONTENT_ERR_NOT_INITIALIZED;
    }
    if (package_id == NULL || package_id[0] == '\0') {
        return OFFLINE_CONTENT_ERR_INVALID_ARGUMENT;
    }
    const size_t index = offline_content_core_find(core, package_id);
    if (index >= core->count) {
        return OFFLINE_CONTENT_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->count; ++cursor) {
        core->entries[cursor] = core->entries[cursor + 1];
    }
    core->count -= 1;
    memset(
        &core->entries[core->count],
        0,
        sizeof(offline_content_entry_t)
    );
    return OFFLINE_CONTENT_OK;
}

bool offline_content_core_next_playable(
    const offline_content_core_t *core,
    offline_content_entry_t *entry_out
) {
    if (core == NULL) {
        return false;
    }
    for (size_t index = 0; index < core->count; ++index) {
        if (!core->entries[index].local_ready) {
            continue;
        }
        if (entry_out != NULL) {
            *entry_out = core->entries[index];
        }
        return true;
    }
    return false;
}
