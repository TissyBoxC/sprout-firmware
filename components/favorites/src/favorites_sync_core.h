#pragma once

#include <stddef.h>

#include "favorites_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Replace the local favourites snapshot with one remote snapshot.
 *
 * Remote ids are trimmed of duplicates and applied newest-first up to the
 * local capacity. An empty remote snapshot clears the local snapshot because
 * the guardian may deliberately remove every favourite. A malformed snapshot
 * leaves the local snapshot unchanged, so a transient provider error cannot
 * erase the guardian's quick-play list.
 */
favorites_error_t favorites_sync_core_replace(
    favorites_core_t *core,
    const char *const *package_ids,
    size_t package_id_count
);

#ifdef __cplusplus
}
#endif
