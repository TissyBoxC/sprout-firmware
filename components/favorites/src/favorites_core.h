#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "content_library.h"
#include "favorites.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure favourites editor state kept newest-first. */
typedef struct {
    char package_ids[FAVORITES_CAPACITY][CONTENT_LIBRARY_ID_SIZE];
    size_t count;
} favorites_core_t;

/** @brief Reset the editor to an empty index. */
void favorites_core_init(favorites_core_t *core);

/** @brief Add one bookmark, keeping newest-first order. */
favorites_error_t favorites_core_add(
    favorites_core_t *core,
    const char *package_id
);

/** @brief Remove one bookmark; a missing id is success. */
favorites_error_t favorites_core_remove(
    favorites_core_t *core,
    const char *package_id
);

/** @brief Toggle membership and report the resulting state. */
favorites_error_t favorites_core_toggle(
    favorites_core_t *core,
    const char *package_id,
    bool *is_favorite_out
);

/** @brief Return true when the id is bookmarked. */
bool favorites_core_contains(
    const favorites_core_t *core,
    const char *package_id
);

#ifdef __cplusplus
}
#endif
