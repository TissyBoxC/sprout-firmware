#include "content_library_index.h"

#include <string.h>

static bool content_index_text_fits(
    const char *value,
    size_t capacity,
    bool allow_empty
) {
    if (value == NULL) {
        return false;
    }
    const size_t length = strnlen(value, capacity);
    if (length >= capacity) {
        return false;
    }
    return allow_empty || length > 0;
}

static bool content_index_hash_is_valid(const char *sha256) {
    if (sha256 == NULL || strlen(sha256) != 64) {
        return false;
    }
    for (size_t index = 0; index < 64; ++index) {
        const char value = sha256[index];
        const bool digit = value >= '0' && value <= '9';
        const bool lower = value >= 'a' && value <= 'f';
        const bool upper = value >= 'A' && value <= 'F';
        if (!digit && !lower && !upper) {
            return false;
        }
    }
    return true;
}

static bool content_index_has_age_tier(
    const content_library_entry_t *entry
) {
    for (size_t index = 0; index < CONTENT_LIBRARY_AGE_TIER_COUNT; ++index) {
        if (entry->age_tiers[index]) {
            return true;
        }
    }
    return false;
}

content_index_result_t content_index_init(
    content_index_t *index,
    content_library_entry_t *entries,
    size_t capacity
) {
    if (index == NULL || entries == NULL || capacity == 0) {
        return CONTENT_INDEX_ERR_INVALID_ARGUMENT;
    }
    memset(index, 0, sizeof(*index));
    index->entries = entries;
    index->capacity = capacity;
    return CONTENT_INDEX_OK;
}

int content_index_compare_version(uint32_t left, uint32_t right) {
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

bool content_index_entry_is_valid(const content_library_entry_t *entry) {
    if (entry == NULL) {
        return false;
    }
    if (!content_index_text_fits(
            entry->package_id,
            sizeof(entry->package_id),
            false
        ) ||
        !content_index_text_fits(
            entry->title,
            sizeof(entry->title),
            true
        ) ||
        !content_index_text_fits(
            entry->category,
            sizeof(entry->category),
            false
        ) ||
        !content_index_text_fits(
            entry->asset_key,
            sizeof(entry->asset_key),
            false
        ) ||
        !content_index_text_fits(
            entry->local_path,
            sizeof(entry->local_path),
            true
        )) {
        return false;
    }
    if (entry->package_version == 0 || !content_index_hash_is_valid(entry->sha256)) {
        return false;
    }
    return content_index_has_age_tier(entry);
}

content_index_result_t content_index_find(
    const content_index_t *index,
    const char *package_id,
    size_t *position_out
) {
    if (index == NULL || index->entries == NULL || package_id == NULL ||
        package_id[0] == '\0') {
        return CONTENT_INDEX_ERR_INVALID_ARGUMENT;
    }
    for (size_t position = 0; position < index->count; ++position) {
        if (strcmp(index->entries[position].package_id, package_id) == 0) {
            if (position_out != NULL) {
                *position_out = position;
            }
            return CONTENT_INDEX_OK;
        }
    }
    return CONTENT_INDEX_ERR_NOT_FOUND;
}

static content_index_result_t content_index_evict_oldest(
    content_index_t *index
) {
    if (index->count == 0) {
        return CONTENT_INDEX_ERR_FULL;
    }
    size_t oldest_position = 0;
    for (size_t position = 1; position < index->count; ++position) {
        if (index->entries[position].published_at <
            index->entries[oldest_position].published_at) {
            oldest_position = position;
        }
    }
    for (size_t position = oldest_position + 1;
         position < index->count;
         ++position) {
        index->entries[position - 1] = index->entries[position];
    }
    --index->count;
    memset(
        &index->entries[index->count],
        0,
        sizeof(index->entries[index->count])
    );
    return CONTENT_INDEX_OK;
}

content_index_result_t content_index_apply_item(
    content_index_t *index,
    const content_library_manifest_item_t *item,
    bool *changed_out
) {
    if (changed_out != NULL) {
        *changed_out = false;
    }
    if (index == NULL || index->entries == NULL || item == NULL) {
        return CONTENT_INDEX_ERR_INVALID_ARGUMENT;
    }
    if (item->operation == CONTENT_LIBRARY_OPERATION_WITHDRAW) {
        const content_index_result_t result = content_index_remove(
            index,
            item->entry.package_id
        );
        if (result == CONTENT_INDEX_OK && changed_out != NULL) {
            *changed_out = true;
        }
        return result == CONTENT_INDEX_ERR_NOT_FOUND ? CONTENT_INDEX_OK : result;
    }
    if (item->operation != CONTENT_LIBRARY_OPERATION_UPSERT ||
        !content_index_entry_is_valid(&item->entry)) {
        return CONTENT_INDEX_ERR_INVALID_ARGUMENT;
    }

    size_t position = 0;
    const content_index_result_t find_result = content_index_find(
        index,
        item->entry.package_id,
        &position
    );
    if (find_result == CONTENT_INDEX_OK) {
        const int comparison = content_index_compare_version(
            item->entry.package_version,
            index->entries[position].package_version
        );
        if (comparison < 0) {
            return CONTENT_INDEX_OK;
        }
        /*
         * Manifest entries describe the published package, not its local
         * download state. Preserve the local path and lifecycle state when
         * the package version is unchanged so a routine sync cannot turn a
         * READY package back into MISSING.
         */
        if (comparison == 0) {
            const content_library_state_t local_state =
                index->entries[position].state;
            const bool local_path_present =
                index->entries[position].local_path[0] != '\0';
            if (strcmp(item->entry.sha256, index->entries[position].sha256) == 0 &&
                strcmp(item->entry.title, index->entries[position].title) == 0 &&
                strcmp(item->entry.category, index->entries[position].category) == 0 &&
                strcmp(item->entry.asset_key, index->entries[position].asset_key) == 0 &&
                item->entry.size_bytes == index->entries[position].size_bytes &&
                item->entry.published_at == index->entries[position].published_at) {
                return CONTENT_INDEX_OK;
            }
            content_library_entry_t merged = item->entry;
            merged.state = local_state;
            if (local_path_present || item->entry.local_path[0] == '\0') {
                memcpy(
                    merged.local_path,
                    index->entries[position].local_path,
                    sizeof(merged.local_path)
                );
            }
            index->entries[position] = merged;
            if (changed_out != NULL) {
                *changed_out = true;
            }
            return CONTENT_INDEX_OK;
        }
        index->entries[position] = item->entry;
        if (changed_out != NULL) {
            *changed_out = true;
        }
        return CONTENT_INDEX_OK;
    }
    if (find_result != CONTENT_INDEX_ERR_NOT_FOUND) {
        return find_result;
    }
    if (index->count >= index->capacity) {
        const content_index_result_t evict_result =
            content_index_evict_oldest(index);
        if (evict_result != CONTENT_INDEX_OK) {
            return evict_result;
        }
    }
    index->entries[index->count] = item->entry;
    ++index->count;
    if (changed_out != NULL) {
        *changed_out = true;
    }
    return CONTENT_INDEX_OK;
}

content_index_result_t content_index_apply_manifest(
    content_index_t *index,
    const content_library_manifest_item_t *items,
    size_t item_count,
    size_t *applied_count_out
) {
    if (applied_count_out != NULL) {
        *applied_count_out = 0;
    }
    if (index == NULL || index->entries == NULL ||
        (items == NULL && item_count > 0)) {
        return CONTENT_INDEX_ERR_INVALID_ARGUMENT;
    }
    size_t applied = 0;
    for (size_t position = 0; position < item_count; ++position) {
        bool changed = false;
        const content_index_result_t result = content_index_apply_item(
            index,
            &items[position],
            &changed
        );
        if (result != CONTENT_INDEX_OK) {
            if (applied_count_out != NULL) {
                *applied_count_out = applied;
            }
            return result;
        }
        if (changed) {
            ++applied;
        }
    }
    if (applied_count_out != NULL) {
        *applied_count_out = applied;
    }
    return CONTENT_INDEX_OK;
}

content_index_result_t content_index_remove(
    content_index_t *index,
    const char *package_id
) {
    size_t position = 0;
    const content_index_result_t find_result = content_index_find(
        index,
        package_id,
        &position
    );
    if (find_result != CONTENT_INDEX_OK) {
        return find_result;
    }
    for (size_t move = position + 1; move < index->count; ++move) {
        index->entries[move - 1] = index->entries[move];
    }
    --index->count;
    memset(
        &index->entries[index->count],
        0,
        sizeof(index->entries[index->count])
    );
    return CONTENT_INDEX_OK;
}

content_index_result_t content_index_oldest(
    const content_index_t *index,
    content_library_entry_t *entry_out
) {
    if (index == NULL || index->entries == NULL || entry_out == NULL ||
        index->count == 0) {
        return CONTENT_INDEX_ERR_INVALID_ARGUMENT;
    }
    size_t oldest_position = 0;
    for (size_t position = 1; position < index->count; ++position) {
        if (index->entries[position].published_at <
            index->entries[oldest_position].published_at) {
            oldest_position = position;
        }
    }
    *entry_out = index->entries[oldest_position];
    return CONTENT_INDEX_OK;
}
