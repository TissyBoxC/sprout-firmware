#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of content items pinned for offline playback. */
#define OFFLINE_CONTENT_CAPACITY 32

/** @brief Size of a content package id including the terminator. */
#define OFFLINE_CONTENT_ID_SIZE 48

/** @brief Size of a content title including the terminator. */
#define OFFLINE_CONTENT_TITLE_SIZE 96

/** @brief Size of a content category including the terminator. */
#define OFFLINE_CONTENT_CATEGORY_SIZE 32

/** @brief Stable offline content error codes for diagnostics and UI copy. */
typedef enum {
    OFFLINE_CONTENT_OK = 0,
    OFFLINE_CONTENT_ERR_NOT_INITIALIZED,
    OFFLINE_CONTENT_ERR_INVALID_ARGUMENT,
    OFFLINE_CONTENT_ERR_FULL,
    OFFLINE_CONTENT_ERR_NOT_FOUND,
    OFFLINE_CONTENT_ERR_STORAGE,
    OFFLINE_CONTENT_ERR_NOT_LOCAL,
} offline_content_error_t;

/** @brief One content package pinned for offline playback. */
typedef struct {
    char package_id[OFFLINE_CONTENT_ID_SIZE];
    char title[OFFLINE_CONTENT_TITLE_SIZE];
    char category[OFFLINE_CONTENT_CATEGORY_SIZE];
    uint32_t package_version;
    uint64_t size_bytes;
    /** Set by the library when the verified package bytes are on disk. */
    bool local_ready;
    int64_t pinned_at_epoch_seconds;
} offline_content_entry_t;

/** @brief Bounded offline content state for telemetry and the device UI. */
typedef struct {
    size_t pinned;
    size_t playable;
    char last_package_id[OFFLINE_CONTENT_ID_SIZE];
} offline_content_snapshot_t;

/** @brief Initialize the offline content index from the configuration store. */
esp_err_t offline_content_init(void);

/** @brief Return true when the offline content index is ready. */
bool offline_content_is_ready(void);

/**
 * @brief Pin or refresh one content package for offline playback.
 *
 * Re-pinning an existing package id replaces it and moves it to the head so
 * the most recently pinned content leads.
 */
esp_err_t offline_content_pin(const offline_content_entry_t *entry);

/**
 * @brief Mark one pinned package playable after the library verified it.
 *
 * Returns OFFLINE_CONTENT_ERR_NOT_FOUND for an unknown package.
 */
esp_err_t offline_content_mark_ready(const char *package_id);

/** @brief Unpin one package. A missing package is success. */
esp_err_t offline_content_unpin(const char *package_id);

/**
 * @brief Pick the next content package to play offline.
 *
 * Only packages whose bytes are verified locally are eligible, so the device
 * never starts a package it cannot play. Newest pinned first.
 */
bool offline_content_next_playable(offline_content_entry_t *entry_out);

/** @brief Copy pinned entries newest pinned first. */
esp_err_t offline_content_list(
    offline_content_entry_t *entries_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return one pinned entry by package id. */
bool offline_content_get(
    const char *package_id,
    offline_content_entry_t *entry_out
);

/** @brief Return the current number of pinned entries. */
size_t offline_content_count(void);

/** @brief Return the bounded offline content snapshot. */
offline_content_snapshot_t offline_content_get_snapshot(void);

/** @brief Return the stable string for one offline content error code. */
const char *offline_content_error_name(offline_content_error_t error);

/** @brief Return the removable-module descriptor for offline_content. */
const module_descriptor_t *offline_content_module_descriptor(void);

#ifdef __cplusplus
}
#endif
