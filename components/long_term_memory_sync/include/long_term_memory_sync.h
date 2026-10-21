#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of locally produced memories reported per batch. */
#define LONG_TERM_MEMORY_SYNC_BATCH_SIZE 8

/** @brief Stable sync status reported to the UI and diagnostics. */
typedef enum {
    LONG_TERM_MEMORY_SYNC_STATUS_IDLE = 0,
    LONG_TERM_MEMORY_SYNC_STATUS_SYNCING,
    LONG_TERM_MEMORY_SYNC_STATUS_UNAUTHORIZED,
    LONG_TERM_MEMORY_SYNC_STATUS_OFFLINE,
    LONG_TERM_MEMORY_SYNC_STATUS_FAILED,
} long_term_memory_sync_status_t;

/** @brief Read-only snapshot of the sync worker state. */
typedef struct {
    long_term_memory_sync_status_t status;
    /** Server revision the local store has applied; 0 before the first pull. */
    int64_t revision;
    /** True when the local store has applied at least one remote snapshot. */
    bool has_snapshot;
    /** Count of local memories waiting for upload. */
    size_t pending_count;
    /** Unix seconds of the last successful pull; 0 when none succeeded yet. */
    int64_t last_success_epoch_seconds;
} long_term_memory_sync_status_snapshot_t;

/**
 * @brief Pull the remote delta, merge it, then push locally produced memories.
 *
 * The request uses the device session token created by
 * `device_binding_client`; a 401/403 clears the token so the next request can
 * re-authenticate instead of retrying a revoked credential. The local store is
 * left untouched when the network, clock, transport, or payload is unusable;
 * an empty snapshot clears it. Locally produced memories are only marked as
 * synced after the platform accepts their batch.
 *
 * @return ESP_OK when the pull applied and every eligible upload was accepted;
 * otherwise the first preparation, transport, or acknowledgement error. A
 * consent-revoked store returns ESP_ERR_INVALID_STATE without any request.
 */
esp_err_t long_term_memory_sync_now(void);

/** @brief Copy the current sync status without exposing any memory value. */
esp_err_t long_term_memory_sync_get_status(
    long_term_memory_sync_status_snapshot_t *status_out
);

/**
 * @brief Queue one locally produced memory key for the next upload.
 *
 * Call this after a successful `long_term_memory_remember` so the entry is
 * reported on the next cycle. The entry stays pending until the platform
 * accepts it, so an offline device still uploads it after a reboot.
 */
esp_err_t long_term_memory_sync_enqueue_key(const char *memory_key);

/** @brief Return the removable-module descriptor for long-term memory sync. */
const module_descriptor_t *long_term_memory_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
