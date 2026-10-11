#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "theme_package.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure theme cache kept newest-change-first. */
typedef struct {
    theme_pack_t packs[THEME_PACKAGE_CAPACITY];
    size_t count;
} theme_package_core_t;

/** @brief Reset the cache to empty. */
void theme_package_core_init(theme_package_core_t *core);

/** @brief Insert or replace one pack, moving it to the head. */
theme_package_error_t theme_package_core_upsert(
    theme_package_core_t *core,
    const theme_pack_t *pack
);

/** @brief Remove one pack; a missing id is success. */
theme_package_error_t theme_package_core_remove(
    theme_package_core_t *core,
    const char *theme_id
);

/** @brief Find one pack index, or count when absent. */
size_t theme_package_core_find(
    const theme_package_core_t *core,
    const char *theme_id
);

#ifdef __cplusplus
}
#endif
