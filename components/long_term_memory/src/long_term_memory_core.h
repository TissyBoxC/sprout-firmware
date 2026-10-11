#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "long_term_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pure long-term memory store keyed by child id and key. */
typedef struct {
    long_term_memory_entry_t entries[LONG_TERM_MEMORY_CAPACITY];
    size_t count;
} long_term_memory_core_t;

/** @brief Reset the store to empty. */
void long_term_memory_core_init(long_term_memory_core_t *core);

/** @brief Insert or replace one entry, matching child id plus key. */
long_term_memory_error_t long_term_memory_core_remember(
    long_term_memory_core_t *core,
    const long_term_memory_entry_t *entry
);

/** @brief Remove one entry by id; a missing id is success. */
long_term_memory_error_t long_term_memory_core_forget(
    long_term_memory_core_t *core,
    const char *entry_id
);

/** @brief Remove every entry for one child, reporting the removed count. */
long_term_memory_error_t long_term_memory_core_forget_child(
    long_term_memory_core_t *core,
    const char *child_id,
    size_t *removed_out
);

/** @brief Find one entry index by child id and key, or count when absent. */
size_t long_term_memory_core_find(
    const long_term_memory_core_t *core,
    const char *child_id,
    const char *key
);

/** @brief Erase every entry, used when authorization is revoked. */
void long_term_memory_core_clear(long_term_memory_core_t *core);

#ifdef __cplusplus
}
#endif
