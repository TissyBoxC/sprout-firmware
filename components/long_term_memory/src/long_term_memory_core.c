#include "long_term_memory_core.h"

#include <string.h>

void long_term_memory_core_init(long_term_memory_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

static bool long_term_memory_entry_is_valid(
    const long_term_memory_entry_t *entry
) {
    if (entry == NULL) {
        return false;
    }
    if (entry->entry_id[0] == '\0' || entry->child_id[0] == '\0') {
        return false;
    }
    if (entry->key[0] == '\0' || entry->value[0] == '\0') {
        return false;
    }
    return true;
}

size_t long_term_memory_core_find(
    const long_term_memory_core_t *core,
    const char *child_id,
    const char *key
) {
    if (core == NULL || child_id == NULL || key == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->count; ++index) {
        const long_term_memory_entry_t *entry = &core->entries[index];
        if (strcmp(entry->child_id, child_id) == 0 &&
            strcmp(entry->key, key) == 0) {
            return index;
        }
    }
    return core->count;
}

long_term_memory_error_t long_term_memory_core_remember(
    long_term_memory_core_t *core,
    const long_term_memory_entry_t *entry
) {
    if (core == NULL) {
        return LONG_TERM_MEMORY_ERR_NOT_INITIALIZED;
    }
    if (!long_term_memory_entry_is_valid(entry)) {
        return LONG_TERM_MEMORY_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = long_term_memory_core_find(
        core,
        entry->child_id,
        entry->key
    );
    if (existing >= core->count && core->count >= LONG_TERM_MEMORY_CAPACITY) {
        return LONG_TERM_MEMORY_ERR_FULL;
    }
    if (existing < core->count) {
        // Re-learning one fact replaces the value in place so the store never
        // grows a duplicate key.
        core->entries[existing] = *entry;
        return LONG_TERM_MEMORY_OK;
    }
    core->entries[core->count] = *entry;
    core->count += 1;
    return LONG_TERM_MEMORY_OK;
}

long_term_memory_error_t long_term_memory_core_forget(
    long_term_memory_core_t *core,
    const char *entry_id
) {
    if (core == NULL) {
        return LONG_TERM_MEMORY_ERR_NOT_INITIALIZED;
    }
    if (entry_id == NULL || entry_id[0] == '\0') {
        return LONG_TERM_MEMORY_ERR_INVALID_ARGUMENT;
    }
    size_t index = core->count;
    for (size_t cursor = 0; cursor < core->count; ++cursor) {
        if (strcmp(core->entries[cursor].entry_id, entry_id) == 0) {
            index = cursor;
            break;
        }
    }
    if (index >= core->count) {
        return LONG_TERM_MEMORY_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->count; ++cursor) {
        core->entries[cursor] = core->entries[cursor + 1];
    }
    core->count -= 1;
    memset(&core->entries[core->count], 0, sizeof(long_term_memory_entry_t));
    return LONG_TERM_MEMORY_OK;
}

long_term_memory_error_t long_term_memory_core_forget_child(
    long_term_memory_core_t *core,
    const char *child_id,
    size_t *removed_out
) {
    if (core == NULL) {
        return LONG_TERM_MEMORY_ERR_NOT_INITIALIZED;
    }
    if (child_id == NULL || child_id[0] == '\0') {
        return LONG_TERM_MEMORY_ERR_INVALID_ARGUMENT;
    }
    size_t removed = 0;
    size_t cursor = 0;
    while (cursor < core->count) {
        if (strcmp(core->entries[cursor].child_id, child_id) == 0) {
            for (size_t shift = cursor; shift + 1 < core->count; ++shift) {
                core->entries[shift] = core->entries[shift + 1];
            }
            core->count -= 1;
            removed += 1;
            continue;
        }
        ++cursor;
    }
    if (removed > 0) {
        memset(
            &core->entries[core->count],
            0,
            sizeof(long_term_memory_entry_t)
        );
    }
    if (removed_out != NULL) {
        *removed_out = removed;
    }
    return LONG_TERM_MEMORY_OK;
}

void long_term_memory_core_clear(long_term_memory_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}
