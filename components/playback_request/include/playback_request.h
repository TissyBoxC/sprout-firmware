#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of guardian playback requests cached on the device. */
#define PLAYBACK_REQUEST_CAPACITY 16

/** @brief Size of a request id including the terminator. */
#define PLAYBACK_REQUEST_ID_SIZE 48

/** @brief Size of a content package id including the terminator. */
#define PLAYBACK_REQUEST_PACKAGE_ID_SIZE 48

/** @brief Size of an optional guardian note including the terminator. */
#define PLAYBACK_REQUEST_NOTE_SIZE 200

/**
 * @brief Maximum number of unacknowledged outbound actions kept for the
 * platform.
 *
 * A request can contribute at most one acknowledgement and one completion, so
 * the queue holds at most twice the local request capacity. Keeping the queue
 * persisted lets the device survive a reboot or an offline window and still
 * deliver every lifecycle change once it reconnects.
 */
#define PLAYBACK_REQUEST_OUTBOUND_CAPACITY \
    (PLAYBACK_REQUEST_CAPACITY * 2)

/**
 * @brief Playback request lifecycle.
 *
 * The numeric values are stable because they cross the persisted blob; keep
 * them in sync with the platform's `pending/acknowledged/completed/failed/
 * cancelled`.
 */
typedef enum {
    PLAYBACK_REQUEST_PENDING = 0,
    PLAYBACK_REQUEST_ACKNOWLEDGED = 1,
    PLAYBACK_REQUEST_COMPLETED = 2,
    PLAYBACK_REQUEST_FAILED = 3,
    PLAYBACK_REQUEST_CANCELLED = 4,
} playback_request_status_t;

/** @brief Terminal outcomes a device reports for one request. */
typedef enum {
    PLAYBACK_REQUEST_OUTCOME_NONE = 0,
    PLAYBACK_REQUEST_OUTCOME_PLAYED = 1,
    PLAYBACK_REQUEST_OUTCOME_SKIPPED = 2,
    PLAYBACK_REQUEST_OUTCOME_ERROR = 3,
} playback_request_outcome_t;

/** @brief The kind of local lifecycle change to report to the platform. */
typedef enum {
    PLAYBACK_REQUEST_OUTBOUND_ACKNOWLEDGE = 0,
    PLAYBACK_REQUEST_OUTBOUND_COMPLETE,
} playback_request_outbound_kind_t;

/** @brief One queued outbound action for the platform. */
typedef struct {
    char request_id[PLAYBACK_REQUEST_ID_SIZE];
    playback_request_outbound_kind_t kind;
    /** Terminal outcome, meaningful only for a COMPLETE action. */
    playback_request_outcome_t outcome;
} playback_request_outbound_t;

/** @brief One cached guardian playback request. */
typedef struct {
    char request_id[PLAYBACK_REQUEST_ID_SIZE];
    char package_id[PLAYBACK_REQUEST_PACKAGE_ID_SIZE];
    playback_request_status_t status;
    playback_request_outcome_t outcome;
    char note[PLAYBACK_REQUEST_NOTE_SIZE];
    int64_t requested_at_epoch_seconds;
} playback_request_t;

/** @brief Stable playback request error codes for diagnostics and UI copy. */
typedef enum {
    PLAYBACK_REQUEST_OK = 0,
    PLAYBACK_REQUEST_ERR_NOT_INITIALIZED,
    PLAYBACK_REQUEST_ERR_INVALID_ARGUMENT,
    PLAYBACK_REQUEST_ERR_FULL,
    PLAYBACK_REQUEST_ERR_NOT_FOUND,
    PLAYBACK_REQUEST_ERR_STORAGE,
    PLAYBACK_REQUEST_ERR_INVALID_STATE,
} playback_request_error_t;

/** @brief Initialize the playback request cache from the configuration store. */
esp_err_t playback_request_init(void);

/** @brief Return true when the playback request cache is ready. */
bool playback_request_is_ready(void);

/** @brief Insert or replace one request, newest change first. */
esp_err_t playback_request_upsert(const playback_request_t *request);

/** @brief Remove one request by id. A missing request is success. */
esp_err_t playback_request_remove(const char *request_id);

/**
 * @brief Merge one remote pending-request snapshot into the local cache.
 *
 * Retains local terminal history, drops local non-terminal requests the
 * guardian cancelled, and ignores a remote entry the device already finished.
 * Unusable or duplicate remote ids are dropped.
 */
esp_err_t playback_request_merge_remote(
    const playback_request_t *remote,
    size_t remote_count
);

/**
 * @brief Mark one request acknowledged.
 *
 * A request that is already terminal is not changed and reports
 * PLAYBACK_REQUEST_ERR_INVALID_STATE.
 */
esp_err_t playback_request_acknowledge(const char *request_id);

/**
 * @brief Record the terminal outcome for one request.
 *
 * `PLAYED`/`SKIPPED` complete the request; `ERROR` fails it. A request that is
 * already terminal is not changed and reports
 * PLAYBACK_REQUEST_ERR_INVALID_STATE.
 */
esp_err_t playback_request_complete(
    const char *request_id,
    playback_request_outcome_t outcome
);

/** @brief Copy cached requests newest-first. */
esp_err_t playback_request_list(
    playback_request_t *requests_out,
    size_t capacity,
    size_t *count_out
);

/**
 * @brief Copy the requests that still need device action (pending or
 * acknowledged), oldest first so the device drains them in order.
 */
esp_err_t playback_request_list_pending(
    playback_request_t *requests_out,
    size_t capacity,
    size_t *count_out
);

/** @brief Return the number of local lifecycle changes waiting to be reported. */
size_t playback_request_pending_outbound_count(void);

/**
 * @brief Copy the queued outbound actions, oldest first.
 *
 * The platform sees each action once: an acknowledgement reports that the
 * device accepted a request, and a completion reports its terminal outcome.
 * Report them in order and drop each one with `playback_request_ack_outbound`
 * after the platform accepts it.
 */
esp_err_t playback_request_list_pending_outbound(
    playback_request_outbound_t *actions_out,
    size_t capacity,
    size_t *count_out
);

/**
 * @brief Drop one reported outbound action from the queue.
 *
 * Called after the platform accepts the action. A missing entry is success so a
 * retried report never fails on an already-drained queue.
 */
esp_err_t playback_request_ack_outbound(
    const char *request_id,
    playback_request_outbound_kind_t kind
);

/** @brief Return one cached request by id. */
bool playback_request_get(const char *request_id, playback_request_t *request_out);

/** @brief Return the current number of cached requests. */
size_t playback_request_count(void);

/** @brief Return the stable string for one request status. */
const char *playback_request_status_name(playback_request_status_t status);

/** @brief Parse a stable status string, or return false when unknown. */
bool playback_request_status_from_name(
    const char *name,
    playback_request_status_t *status_out
);

/** @brief Return the stable string for one request outcome. */
const char *playback_request_outcome_name(playback_request_outcome_t outcome);

/** @brief Parse a stable outcome string, or return false when unknown. */
bool playback_request_outcome_from_name(
    const char *name,
    playback_request_outcome_t *outcome_out
);

/** @brief Return the stable string for one playback request error code. */
const char *playback_request_error_name(playback_request_error_t error);

/** @brief Return the removable-module descriptor for playback_request. */
const module_descriptor_t *playback_request_module_descriptor(void);

#ifdef __cplusplus
}
#endif
