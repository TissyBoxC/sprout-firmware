#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum session identifier length accepted from a caller. */
#define ASSESSMENT_SYNC_SESSION_ID_SIZE 64

/** @brief Maximum item identifier length accepted from the platform. */
#define ASSESSMENT_SYNC_ITEM_ID_SIZE 129

/** @brief Maximum item type length (`word` or `sentence`). */
#define ASSESSMENT_SYNC_ITEM_TYPE_SIZE 16

/**
 * @brief Maximum target text length the child is asked to say.
 *
 * Matches the platform bound of 160 characters, plus the terminator.
 */
#define ASSESSMENT_SYNC_TARGET_TEXT_SIZE 161

/** @brief Maximum session state length (`active`, `completed`, ...). */
#define ASSESSMENT_SYNC_STATE_SIZE 16

/** @brief Maximum number of queued answers awaiting upload. */
#define ASSESSMENT_SYNC_PENDING_CAPACITY 8

/**
 * @brief Stable sync status reported to diagnostics.
 *
 * The values describe the worker, never the child's performance.
 */
typedef enum {
    ASSESSMENT_SYNC_STATUS_IDLE = 0,
    ASSESSMENT_SYNC_STATUS_SYNCING,
    ASSESSMENT_SYNC_STATUS_UNAUTHORIZED,
    ASSESSMENT_SYNC_STATUS_OFFLINE,
    ASSESSMENT_SYNC_STATUS_FAILED,
} assessment_sync_status_t;

/**
 * @brief One item the device is asked to present.
 *
 * `text` is the course content the child should say, not a transcript of the
 * child. It is the only field the device may render.
 */
typedef struct {
    bool has_item;
    int32_t position;
    int32_t difficulty;
    char item_id[ASSESSMENT_SYNC_ITEM_ID_SIZE];
    char item_type[ASSESSMENT_SYNC_ITEM_TYPE_SIZE];
    char text[ASSESSMENT_SYNC_TARGET_TEXT_SIZE];
} assessment_sync_item_t;

/** @brief Read-only snapshot of the session and worker state. */
typedef struct {
    assessment_sync_status_t status;
    bool has_session;
    /** Session state string; empty when no session is being tracked. */
    char state[ASSESSMENT_SYNC_STATE_SIZE];
    /** True when the platform still accepts answers for the tracked session. */
    bool accepts_answers;
    int32_t difficulty;
    int32_t answered_count;
    int32_t correct_count;
    int32_t mastery;
    /** Current item to present; `has_item` is false once the run is over. */
    assessment_sync_item_t item;
    /** Answers queued locally and not yet accepted by the platform. */
    size_t pending_count;
    /** Unix seconds of the last successful pull; 0 when none succeeded. */
    int64_t last_success_epoch_seconds;
} assessment_sync_status_snapshot_t;

/**
 * @brief Fetch the current item of one assessment session.
 *
 * The request uses the device session token created by
 * `device_binding_client`; a 401/403 clears the token so the next request can
 * re-authenticate instead of retrying a revoked credential. The tracked
 * session is left untouched when the network, clock, transport, or payload is
 * unusable.
 *
 * The returned item is only recorded in the status snapshot and logged by
 * identifier: presenting it and grading the child stay with the guardian.
 *
 * @return ESP_OK when the session was read and applied; otherwise the
 *         preparation, transport, or payload error.
 */
esp_err_t assessment_sync_refresh_session(const char *session_id);

/**
 * @brief Queue one answer graded by the guardian for upload.
 *
 * The firmware has no speech recognition, so it never derives a score. This
 * function only accepts a score the guardian supplied on their phone; it
 * stores the answer until the platform accepts it, so an offline device
 * uploads it after reconnecting.
 *
 * Calling it with a score outside 0-100, a session that is not being tracked,
 * or an item that does not match the tracked item is refused.
 *
 * @return ESP_OK when the answer was queued; ESP_ERR_INVALID_ARG for a
 *         malformed answer; ESP_ERR_INVALID_STATE when no session is tracked;
 *         ESP_ERR_NO_MEM when the queue is full.
 */
esp_err_t assessment_sync_submit_answer(
    const char *session_id,
    const char *item_id,
    const char *item_type,
    const char *target_text,
    int score
);

/** @brief Copy the current status without exposing any transcript. */
esp_err_t assessment_sync_get_status(
    assessment_sync_status_snapshot_t *status_out
);

/**
 * @brief Clear the tracked session and every queued answer.
 *
 * Called when the guardian finishes or abandons the run, so the next session
 * starts from an empty queue.
 */
esp_err_t assessment_sync_reset(void);

/** @brief Return the removable-module descriptor for assessment sync. */
const module_descriptor_t *assessment_sync_module_descriptor(void);

#ifdef __cplusplus
}
#endif
