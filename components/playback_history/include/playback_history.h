#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_library.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of playback records kept on the device. */
#define PLAYBACK_HISTORY_CAPACITY 32

/**
 * @brief Maximum number of playback records waiting to be reported.
 *
 * The device reports each finished play to the platform once. The queue is
 * persisted so an offline device still delivers its recent playback history
 * after it reconnects, and it is bounded to the same size as the ring so it
 * cannot grow without limit.
 */
#define PLAYBACK_HISTORY_OUTBOUND_CAPACITY PLAYBACK_HISTORY_CAPACITY

/** @brief How one playback attempt ended; mirrors the platform contract. */
typedef enum {
    PLAYBACK_OUTCOME_COMPLETED = 0,
    PLAYBACK_OUTCOME_INTERRUPTED,
} playback_outcome_t;

/** @brief One bounded playback history row. */
typedef struct {
    char package_id[CONTENT_LIBRARY_ID_SIZE];
    uint32_t package_version;
    playback_outcome_t outcome;
    uint32_t played_seconds;
    int64_t played_at;
} playback_record_t;

/** @brief Stable playback-history error codes for diagnostics and UI copy. */
typedef enum {
    PLAYBACK_HISTORY_OK = 0,
    PLAYBACK_HISTORY_ERR_NOT_INITIALIZED,
    PLAYBACK_HISTORY_ERR_INVALID_ARGUMENT,
    PLAYBACK_HISTORY_ERR_STORAGE,
} playback_history_error_t;

/** @brief Initialize the history index from the configuration store. */
esp_err_t playback_history_init(void);

/** @brief Return true when the history index is ready. */
bool playback_history_is_ready(void);

/**
 * @brief Record one playback event.
 *
 * Newest records lead. When the ring is full the oldest record is dropped so
 * the table cannot grow without limit. Returns the newest->oldest index of the
 * recorded row (0) on success via index_out when provided.
 */
esp_err_t playback_history_record(
    const playback_record_t *record,
    size_t *index_out
);

/** @brief Copy up to capacity records newest-first and report the true count. */
esp_err_t playback_history_list(
    playback_record_t *records_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return the current number of records. */
size_t playback_history_count(void);

/** @brief Return the number of records waiting to be reported to the platform. */
size_t playback_history_pending_count(void);

/**
 * @brief Copy the records waiting to be reported, oldest first.
 *
 * Report them in order and drop each one with `playback_history_ack_pending`
 * after the platform accepts it.
 */
esp_err_t playback_history_list_pending(
    playback_record_t *records_out,
    size_t capacity,
    size_t *count_out
);

/**
 * @brief Drop one reported record from the pending queue.
 *
 * Keyed by (package id, played_at) because a package can be played more than
 * once. A missing entry is success so a retried report never fails.
 */
esp_err_t playback_history_ack_pending(
    const char *package_id,
    int64_t played_at
);

/**
 * @brief Remove every recorded playback row.
 *
 * Used by the privacy deletion path so a child's content history cannot
 * survive a guardian withdrawal, unbind, or factory reset.
 */
esp_err_t playback_history_forget_all(void);

/**
 * @brief Return the most recent interrupted-but-resumable record.
 *
 * A completed play is not resumable; the newest interrupted record wins so the
 * device can offer "继续播放".
 */
bool playback_history_latest_resumable(playback_record_t *record_out);

/** @brief Return the stable string for one playback outcome. */
const char *playback_outcome_name(playback_outcome_t outcome);

/** @brief Return the stable string for one history error code. */
const char *playback_history_error_name(playback_history_error_t error);

/** @brief Return the removable-module descriptor for playback_history. */
const module_descriptor_t *playback_history_module_descriptor(void);

#ifdef __cplusplus
}
#endif
