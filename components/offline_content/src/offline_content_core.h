#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "offline_content.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure offline content store kept newest-pinned-first. */
typedef struct {
    offline_content_entry_t entries[OFFLINE_CONTENT_CAPACITY];
    size_t count;
} offline_content_core_t;

/** @brief Reset the store to empty. */
void offline_content_core_init(offline_content_core_t *core);

/** @brief Pin or replace one entry, moving it to the head. */
offline_content_error_t offline_content_core_pin(
    offline_content_core_t *core,
    const offline_content_entry_t *entry
);

/** @brief Find one entry index, or count when absent. */
size_t offline_content_core_find(
    const offline_content_core_t *core,
    const char *package_id
);

/** @brief Unpin one entry; a missing id is success. */
offline_content_error_t offline_content_core_unpin(
    offline_content_core_t *core,
    const char *package_id
);

/** @brief Mark one entry ready; unknown id returns NOT_FOUND. */
offline_content_error_t offline_content_core_mark_ready(
    offline_content_core_t *core,
    const char *package_id
);

/** @brief Pick the newest ready entry, or false when none is playable. */
bool offline_content_core_next_playable(
    const offline_content_core_t *core,
    offline_content_entry_t *entry_out
);

#ifdef __cplusplus
}
#endif
