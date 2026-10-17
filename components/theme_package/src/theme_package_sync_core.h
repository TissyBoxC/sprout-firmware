#pragma once

#include <stddef.h>

#include "theme_package_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Replace the local theme cache with one remote snapshot.
 *
 * Remote packs are applied in order, dropping duplicate theme ids and any pack
 * whose title or members are unusable. A snapshot that cannot fit the local
 * capacity in full is rejected rather than silently truncated. An empty remote
 * snapshot clears the cache because the guardian may deliberately withdraw
 * every theme. A malformed snapshot leaves the cache unchanged, so a transient
 * provider error cannot erase the thematic menu.
 */
theme_package_error_t theme_package_sync_core_replace(
    theme_package_core_t *core,
    const theme_pack_t *packs,
    size_t pack_count
);

#ifdef __cplusplus
}
#endif
