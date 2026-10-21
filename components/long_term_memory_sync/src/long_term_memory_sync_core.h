#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "long_term_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of remote entries applied by one pull cycle. */
#define LONG_TERM_MEMORY_SYNC_REMOTE_CAPACITY LONG_TERM_MEMORY_CAPACITY

/** @brief Maximum number of locally produced entries reported per push. */
#define LONG_TERM_MEMORY_SYNC_BATCH_CAPACITY 8

/** @brief Maximum number of locally produced keys awaiting upload. */
#define LONG_TERM_MEMORY_SYNC_PENDING_CAPACITY 16

/**
 * @brief Confidence reported for a memory the device produced itself.
 *
 * A fact the child stated directly is the strongest signal the platform can
 * receive, so it is uploaded at full weight. The platform stores confidence as
 * a 0..1 ratio; the internal Q16 store is a separate representation and must
 * be converted at the wire boundary, otherwise the whole upload is rejected.
 */
#define LONG_TERM_MEMORY_SYNC_UPLOAD_CONFIDENCE 1.0

/** @brief Full weight in the internal Q16 confidence representation. */
#define LONG_TERM_MEMORY_SYNC_CONFIDENCE_MAX 65536

/** @brief One remote memory as delivered by the pull endpoint. */
typedef struct {
    char memory_key[LONG_TERM_MEMORY_KEY_SIZE];
    char memory_value[LONG_TERM_MEMORY_VALUE_SIZE];
    int32_t confidence;
    int64_t revision;
    int64_t updated_at_epoch_seconds;
} long_term_memory_sync_remote_t;

/** @brief Run-wide sync bookkeeping persisted next to the local memory store. */
typedef struct {
    int64_t revision;
    bool has_snapshot;
} long_term_memory_sync_state_t;

/**
 * @brief Reset the run state so the next pull asks for a full snapshot.
 *
 * Used when a revision is unusable, so the device can never claim to hold a
 * snapshot it has not fully applied.
 */
void long_term_memory_sync_state_reset(long_term_memory_sync_state_t *state);

/**
 * @brief Return true when the local memory already holds this key.
 *
 * Remote entries must not overwrite a fact the child learned locally before it
 * was uploaded, otherwise the server's older copy would win forever.
 */
bool long_term_memory_sync_holds_key(
    const long_term_memory_entry_t *entries,
    size_t entry_count,
    const char *child_id,
    const char *memory_key
);

/**
 * @brief Parse one pull snapshot into a bounded remote list.
 *
 * Entries with an unusable key or value, an out-of-range confidence, or a
 * duplicate key are skipped. Returns false when the list cannot be trusted:
 * more entries than the local capacity, a non-empty snapshot that reduces to
 * nothing usable, or a revision that moves backwards.
 */
bool long_term_memory_sync_parse_remote(
    long_term_memory_sync_remote_t *remote_out,
    size_t capacity,
    size_t *remote_count_out,
    size_t entry_count,
    const char *const *keys,
    const char *const *values,
    const int32_t *confidences,
    const int64_t *revisions,
    const int64_t *updated_at_epoch_seconds,
    int64_t response_revision,
    int64_t current_revision,
    bool has_snapshot
);

/**
 * @brief Build the child id a remote memory is stored under on this device.
 *
 * The backend keys memories per device, and the device only knows one child,
 * so a synced memory is stored under the device prefix instead of a learned
 * child id.
 */
bool long_term_memory_sync_child_id(
    char *destination,
    size_t destination_size,
    const char *device_id
);

/** @brief Return true when one key is tracked in the pending upload list. */
bool long_term_memory_sync_pending_contains(
    const char *const *pending_keys,
    size_t pending_count,
    const char *memory_key
);

/**
 * @brief Add one locally produced key to the pending upload list.
 *
 * A key is tracked once. Returns false when the key is unusable or the list is
 * already full; the caller keeps the entry local and retries after the next
 * successful batch.
 */
bool long_term_memory_sync_pending_add(
    char (*pending_keys)[LONG_TERM_MEMORY_KEY_SIZE],
    size_t capacity,
    size_t *pending_count_in_out,
    const char *memory_key
);

#ifdef __cplusplus
}
#endif
