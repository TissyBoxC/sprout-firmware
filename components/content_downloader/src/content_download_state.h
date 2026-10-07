#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "content_downloader.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Request headers calculated for one transfer attempt. */
typedef struct {
    bool use_range;
    uint64_t range_start;
    uint64_t range_end;
} content_download_range_t;

/** @brief One transfer state transition result. */
typedef struct {
    content_download_status_t status;
    content_downloader_error_t error;
} content_download_transition_t;

/** @brief Pure transfer state kept outside the ESP-IDF task. */
typedef struct {
    content_download_request_t request;
    content_download_status_t status;
    content_downloader_error_t last_error;
    uint64_t received_bytes;
    uint64_t total_bytes;
    int attempt;
    int max_attempts;
    bool canceled;
    bool retry_requested;
} content_download_state_t;

/** @brief Initialize a state machine for one request. */
bool content_download_state_init(
    content_download_state_t *state,
    const content_download_request_t *request,
    int max_attempts
);

/** @brief Start or retry a transfer, clearing the cancellation flag. */
content_download_transition_t content_download_state_start(
    content_download_state_t *state
);

/**
 * @brief Return the Range header for the current partial file.
 *
 * A zero offset requests a full download. When the server ignores Range, the
 * caller truncates the file and restarts from offset zero.
 */
content_download_range_t content_download_state_range(
    const content_download_state_t *state
);

/** @brief Apply a server response status and Content-Range decision. */
content_download_transition_t content_download_state_apply_response(
    content_download_state_t *state,
    int http_status,
    uint64_t requested_offset,
    uint64_t content_range_start,
    uint64_t content_range_end,
    uint64_t content_length
);

/** @brief Record streamed bytes and keep the size bound enforced. */
content_download_transition_t content_download_state_record_bytes(
    content_download_state_t *state,
    size_t byte_count
);

/** @brief Enter verification after the transport completed. */
content_download_transition_t content_download_state_begin_verify(
    content_download_state_t *state
);

/** @brief Complete verification successfully. */
content_download_transition_t content_download_state_complete(
    content_download_state_t *state
);

/** @brief Record a failure and decide whether a retry remains. */
content_download_transition_t content_download_state_fail(
    content_download_state_t *state,
    content_downloader_error_t error
);

/** @brief Request cancellation at the next safe boundary. */
content_download_transition_t content_download_state_cancel(
    content_download_state_t *state
);

/** @brief Return true when the transfer may continue. */
bool content_download_state_is_active(const content_download_state_t *state);

/** @brief Return the progress structure for the UI and package manager. */
content_download_progress_t content_download_state_progress(
    const content_download_state_t *state
);

#ifdef __cplusplus
}
#endif
