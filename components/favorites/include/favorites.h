#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_library.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of guardian favourites kept on the device. */
#define FAVORITES_CAPACITY 24

/** @brief Stable favourites error codes for diagnostics and UI copy. */
typedef enum {
    FAVORITES_OK = 0,
    FAVORITES_ERR_NOT_INITIALIZED,
    FAVORITES_ERR_INVALID_ARGUMENT,
    FAVORITES_ERR_FULL,
    FAVORITES_ERR_NOT_FOUND,
    FAVORITES_ERR_STORAGE,
} favorites_error_t;

/** @brief Initialize the favourites index from the configuration store. */
esp_err_t favorites_init(void);

/** @brief Return true when the favourites index is ready. */
bool favorites_is_ready(void);

/** @brief Bookmark one package id. A repeat add is success. */
esp_err_t favorites_add(const char *package_id);

/** @brief Remove one bookmark. A missing bookmark is success. */
esp_err_t favorites_remove(const char *package_id);

/**
 * @brief Atomically replace every bookmark with one authoritative snapshot.
 *
 * An empty snapshot clears the list. Invalid ids are ignored, but a non-empty
 * snapshot containing only invalid ids is rejected so a malformed remote
 * response cannot erase the local list.
 */
esp_err_t favorites_replace(
    const char *const *package_ids,
    size_t package_id_count
);

/** @brief Toggle one bookmark, reporting the resulting membership. */
esp_err_t favorites_toggle(const char *package_id, bool *is_favorite_out);

/** @brief Return true when the package id is bookmarked. */
bool favorites_contains(const char *package_id);

/**
 * @brief Copy the ordered favourites list.
 *
 * Writes at most capacity ids into package_ids_out and reports the total in
 * count_out. Newest bookmarks lead so the device can offer a quick-play menu.
 */
esp_err_t favorites_list(
    char (*package_ids_out)[CONTENT_LIBRARY_ID_SIZE],
    size_t capacity,
    size_t *count_out
);

/** @brief Return the current number of bookmarks. */
size_t favorites_count(void);

/** @brief Return the stable string for one favourites error code. */
const char *favorites_error_name(favorites_error_t error);

/** @brief Return the removable-module descriptor for favorites. */
const module_descriptor_t *favorites_module_descriptor(void);

#ifdef __cplusplus
}
#endif
