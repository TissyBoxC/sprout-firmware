#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum package identifier length including the terminator. */
#define CONTENT_LIBRARY_ID_SIZE 48

/** @brief Maximum human-readable package title length including terminator. */
#define CONTENT_LIBRARY_TITLE_SIZE 96

/** @brief Maximum category name length including the terminator. */
#define CONTENT_LIBRARY_CATEGORY_SIZE 32

/** @brief Maximum asset key length including the terminator. */
#define CONTENT_LIBRARY_ASSET_KEY_SIZE 96

/** @brief Maximum content path length including the terminator. */
#define CONTENT_LIBRARY_PATH_SIZE 128

/** @brief Number of age tiers tracked per package. */
#define CONTENT_LIBRARY_AGE_TIER_COUNT 4

/** @brief Content lifecycle states. */
typedef enum {
    CONTENT_LIBRARY_STATE_MISSING = 0,
    CONTENT_LIBRARY_STATE_DOWNLOADING,
    CONTENT_LIBRARY_STATE_READY,
    CONTENT_LIBRARY_STATE_FAILED,
    CONTENT_LIBRARY_STATE_WITHDRAWN,
} content_library_state_t;

/** @brief One locally indexed content package. */
typedef struct {
    char package_id[CONTENT_LIBRARY_ID_SIZE];
    uint32_t package_version;
    char title[CONTENT_LIBRARY_TITLE_SIZE];
    char category[CONTENT_LIBRARY_CATEGORY_SIZE];
    bool age_tiers[CONTENT_LIBRARY_AGE_TIER_COUNT];
    char asset_key[CONTENT_LIBRARY_ASSET_KEY_SIZE];
    char sha256[65];
    uint64_t size_bytes;
    int64_t published_at;
    char local_path[CONTENT_LIBRARY_PATH_SIZE];
    content_library_state_t state;
} content_library_entry_t;

/** @brief Incremental manifest operation applied to one entry. */
typedef enum {
    CONTENT_LIBRARY_OPERATION_UPSERT = 0,
    CONTENT_LIBRARY_OPERATION_WITHDRAW,
} content_library_operation_t;

/** @brief One manifest entry consumed by the incremental merge. */
typedef struct {
    content_library_operation_t operation;
    content_library_entry_t entry;
} content_library_manifest_item_t;

/** @brief Stable library error codes for diagnostics and UI copy. */
typedef enum {
    CONTENT_LIBRARY_OK = 0,
    CONTENT_LIBRARY_ERR_NOT_INITIALIZED,
    CONTENT_LIBRARY_ERR_INVALID_ARGUMENT,
    CONTENT_LIBRARY_ERR_FULL,
    CONTENT_LIBRARY_ERR_NOT_FOUND,
    CONTENT_LIBRARY_ERR_STORAGE,
    CONTENT_LIBRARY_ERR_CORRUPT_INDEX,
} content_library_error_t;

/** @brief Initialize the index from the configuration store. */
esp_err_t content_library_init(void);

/** @brief Return true when the index has loaded successfully. */
bool content_library_is_ready(void);

/** @brief Return the number of indexed entries. */
size_t content_library_count(void);

/**
 * @brief Merge a batch of manifest operations into the index.
 *
 * Newer package versions replace older versions for the same package id.
 * Equal versions are idempotent. Withdraw removes the entry and its persisted
 * record. When capacity is exhausted the oldest published_at entry is evicted.
 */
esp_err_t content_library_apply_manifest(
    const content_library_manifest_item_t *items,
    size_t item_count
);

/**
 * @brief Copy one entry by package identifier.
 *
 * Returns CONTENT_LIBRARY_ERR_NOT_FOUND when the package is not indexed.
 */
esp_err_t content_library_get(
    const char *package_id,
    content_library_entry_t *entry_out
);

/**
 * @brief List entries matching a category and age tier.
 *
 * category may be NULL to accept every category. age_tier is zero based and
 * must be less than CONTENT_LIBRARY_AGE_TIER_COUNT; pass -1 to accept all.
 * Only CONTENT_LIBRARY_STATE_READY entries are returned unless include_all is
 * true. The function writes as many entries as fit and returns the total match
 * count through match_count_out.
 */
esp_err_t content_library_list(
    const char *category,
    int age_tier,
    bool include_all,
    content_library_entry_t *entries_out,
    size_t entry_capacity,
    size_t *match_count_out
);

/** @brief Update the lifecycle state and persist the index. */
esp_err_t content_library_update_state(
    const char *package_id,
    content_library_state_t state
);

/**
 * @brief Remove one package from the index and persist the change.
 *
 * The caller owns deleting the package file, if one exists.
 */
esp_err_t content_library_remove(const char *package_id);

/** @brief Return the stable string for one state. */
const char *content_library_state_name(content_library_state_t state);

/** @brief Return the stable string for one error code. */
const char *content_library_error_name(content_library_error_t error);

/** @brief Return the removable-module descriptor for content_library. */
const module_descriptor_t *content_library_module_descriptor(void);

#ifdef __cplusplus
}
#endif
