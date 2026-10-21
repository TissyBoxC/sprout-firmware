#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "long_term_memory_sync_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Persisted sync bookkeeping, kept apart from the local memory blob. */
typedef struct {
    long_term_memory_sync_state_t state;
    char pending_keys[LONG_TERM_MEMORY_SYNC_PENDING_CAPACITY]
                     [LONG_TERM_MEMORY_KEY_SIZE];
    size_t pending_count;
} long_term_memory_sync_store_t;

/**
 * @brief Load the sync bookkeeping from the configuration store.
 *
 * A missing or corrupt record starts from "no snapshot, nothing pending" so a
 * damaged record forces a full pull instead of trusting a revision the store
 * cannot back up.
 */
esp_err_t long_term_memory_sync_store_load(
    long_term_memory_sync_store_t *store_out
);

/** @brief Persist the sync bookkeeping; a failed write is reported. */
esp_err_t long_term_memory_sync_store_save(
    const long_term_memory_sync_store_t *store
);

#ifdef __cplusplus
}
#endif
