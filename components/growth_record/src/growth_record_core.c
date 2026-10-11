#include "growth_record_core.h"

#include <string.h>

void growth_record_core_init(growth_record_core_t *core) {
    if (core == NULL) {
        return;
    }
    memset(core, 0, sizeof(*core));
}

static bool growth_record_category_is_valid(growth_category_t category) {
    switch (category) {
        case GROWTH_CATEGORY_MILESTONE:
        case GROWTH_CATEGORY_ENCOURAGEMENT:
        case GROWTH_CATEGORY_HABIT:
        case GROWTH_CATEGORY_EMOTION:
            return true;
        default:
            return false;
    }
}

static bool growth_record_is_valid(const growth_record_t *record) {
    if (record == NULL) {
        return false;
    }
    if (record->record_id[0] == '\0' || record->title[0] == '\0') {
        return false;
    }
    return growth_record_category_is_valid(record->category);
}

size_t growth_record_core_find(
    const growth_record_core_t *core,
    const char *record_id
) {
    if (core == NULL || record_id == NULL) {
        return 0;
    }
    for (size_t index = 0; index < core->count; ++index) {
        if (strcmp(core->records[index].record_id, record_id) == 0) {
            return index;
        }
    }
    return core->count;
}

growth_record_error_t growth_record_core_upsert(
    growth_record_core_t *core,
    const growth_record_t *record
) {
    if (core == NULL) {
        return GROWTH_RECORD_ERR_NOT_INITIALIZED;
    }
    if (!growth_record_is_valid(record)) {
        return GROWTH_RECORD_ERR_INVALID_ARGUMENT;
    }
    const size_t existing = growth_record_core_find(core, record->record_id);
    if (existing >= core->count && core->count >= GROWTH_RECORD_CAPACITY) {
        return GROWTH_RECORD_ERR_FULL;
    }
    if (existing < core->count) {
        // Replace in place: move the existing entry to the head without
        // changing the count.
        for (size_t index = existing; index > 0; --index) {
            core->records[index] = core->records[index - 1];
        }
        core->records[0] = *record;
        return GROWTH_RECORD_OK;
    }
    // Insert a new record: shift every entry down, dropping the oldest when
    // the store is already full.
    const size_t last = core->count < GROWTH_RECORD_CAPACITY
        ? core->count
        : GROWTH_RECORD_CAPACITY - 1;
    for (size_t index = last; index > 0; --index) {
        core->records[index] = core->records[index - 1];
    }
    core->records[0] = *record;
    core->count += 1;
    return GROWTH_RECORD_OK;
}

growth_record_error_t growth_record_core_remove(
    growth_record_core_t *core,
    const char *record_id
) {
    if (core == NULL) {
        return GROWTH_RECORD_ERR_NOT_INITIALIZED;
    }
    if (record_id == NULL || record_id[0] == '\0') {
        return GROWTH_RECORD_ERR_INVALID_ARGUMENT;
    }
    const size_t index = growth_record_core_find(core, record_id);
    if (index >= core->count) {
        return GROWTH_RECORD_OK;
    }
    for (size_t cursor = index; cursor + 1 < core->count; ++cursor) {
        core->records[cursor] = core->records[cursor + 1];
    }
    core->count -= 1;
    memset(&core->records[core->count], 0, sizeof(growth_record_t));
    return GROWTH_RECORD_OK;
}

growth_record_error_t growth_record_core_remove_child(
    growth_record_core_t *core,
    const char *child_id,
    size_t *removed_out
) {
    if (core == NULL) {
        return GROWTH_RECORD_ERR_NOT_INITIALIZED;
    }
    if (child_id == NULL || child_id[0] == '\0') {
        return GROWTH_RECORD_ERR_INVALID_ARGUMENT;
    }
    size_t removed = 0;
    size_t cursor = 0;
    while (cursor < core->count) {
        if (strcmp(core->records[cursor].child_id, child_id) == 0) {
            for (size_t shift = cursor; shift + 1 < core->count; ++shift) {
                core->records[shift] = core->records[shift + 1];
            }
            core->count -= 1;
            removed += 1;
            continue;
        }
        ++cursor;
    }
    if (removed > 0) {
        memset(&core->records[core->count], 0, sizeof(growth_record_t));
    }
    if (removed_out != NULL) {
        *removed_out = removed;
    }
    return GROWTH_RECORD_OK;
}
