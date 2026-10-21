#include "long_term_memory_sync_store.h"

#include <string.h>

#include "config_store.h"

// The sync record lives under its own key so removing this component leaves
// the long-term memory blob intact and local-only operation unaffected.
#define LONG_TERM_MEMORY_SYNC_STORE_KEY "ltm_sync"
CONFIG_STORE_KEY_ASSERT(LONG_TERM_MEMORY_SYNC_STORE_KEY);

#define LONG_TERM_MEMORY_SYNC_STORE_VERSION 1u

/** @brief Persisted envelope: a version tag in front of the sync record. */
typedef struct {
    uint32_t version;
    long_term_memory_sync_store_t store;
} long_term_memory_sync_blob_t;

static bool long_term_memory_sync_store_is_valid(
    const long_term_memory_sync_store_t *store
) {
    if (store->pending_count > LONG_TERM_MEMORY_SYNC_PENDING_CAPACITY) {
        return false;
    }
    if (store->state.revision < 0) {
        return false;
    }
    // Every retained key must be a terminated, non-empty string, otherwise a
    // corrupt length would leak unterminated data into a request body.
    for (size_t index = 0; index < store->pending_count; ++index) {
        const char *key = store->pending_keys[index];
        if (key[0] == '\0' ||
            memchr(key, '\0', LONG_TERM_MEMORY_KEY_SIZE) == NULL) {
            return false;
        }
    }
    return true;
}

esp_err_t long_term_memory_sync_store_load(
    long_term_memory_sync_store_t *store_out
) {
    if (store_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(store_out, 0, sizeof(*store_out));
    long_term_memory_sync_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    size_t size = sizeof(blob);
    const esp_err_t read = config_store_get_blob(
        LONG_TERM_MEMORY_SYNC_STORE_KEY,
        &blob,
        &size
    );
    if (read != ESP_OK || size != sizeof(blob) ||
        blob.version != LONG_TERM_MEMORY_SYNC_STORE_VERSION ||
        !long_term_memory_sync_store_is_valid(&blob.store)) {
        long_term_memory_sync_state_reset(&store_out->state);
        return read == ESP_OK ? ESP_ERR_INVALID_STATE : read;
    }
    *store_out = blob.store;
    return ESP_OK;
}

esp_err_t long_term_memory_sync_store_save(
    const long_term_memory_sync_store_t *store
) {
    if (store == NULL || !long_term_memory_sync_store_is_valid(store)) {
        return ESP_ERR_INVALID_ARG;
    }
    long_term_memory_sync_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.version = LONG_TERM_MEMORY_SYNC_STORE_VERSION;
    blob.store = *store;
    return config_store_set_blob(
        LONG_TERM_MEMORY_SYNC_STORE_KEY,
        &blob,
        sizeof(blob)
    );
}
