#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_library.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result codes returned by the pure index implementation. */
typedef enum {
    CONTENT_INDEX_OK = 0,
    CONTENT_INDEX_ERR_INVALID_ARGUMENT,
    CONTENT_INDEX_ERR_FULL,
    CONTENT_INDEX_ERR_NOT_FOUND,
} content_index_result_t;

/** @brief Persistent index container. */
typedef struct {
    content_library_entry_t *entries;
    size_t capacity;
    size_t count;
} content_index_t;

/** @brief Bind a caller-owned entry array to the index. */
content_index_result_t content_index_init(
    content_index_t *index,
    content_library_entry_t *entries,
    size_t capacity
);

/**
 * @brief Compare package versions.
 *
 * Returns a negative value when left is older, zero when equal, and a positive
 * value when left is newer.
 */
int content_index_compare_version(uint32_t left, uint32_t right);

/** @brief Validate the mandatory identifier, hash, category, and asset fields. */
bool content_index_entry_is_valid(const content_library_entry_t *entry);

/** @brief Find one entry by package identifier. */
content_index_result_t content_index_find(
    const content_index_t *index,
    const char *package_id,
    size_t *position_out
);

/**
 * @brief Apply one manifest item with incremental semantics.
 *
 * A newer version replaces the old entry, an older version is ignored, and an
 * equal version refreshes non-identity fields. A withdrawal removes the entry.
 */
content_index_result_t content_index_apply_item(
    content_index_t *index,
    const content_library_manifest_item_t *item,
    bool *changed_out
);

/** @brief Apply a batch, stopping at the first invalid item. */
content_index_result_t content_index_apply_manifest(
    content_index_t *index,
    const content_library_manifest_item_t *items,
    size_t item_count,
    size_t *applied_count_out
);

/** @brief Remove one entry by identifier. */
content_index_result_t content_index_remove(
    content_index_t *index,
    const char *package_id
);

/** @brief Copy the entry with the oldest published_at value. */
content_index_result_t content_index_oldest(
    const content_index_t *index,
    content_library_entry_t *entry_out
);

#ifdef __cplusplus
}
#endif
