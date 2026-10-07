#include "content_download_state.h"

#include <string.h>

static content_download_transition_t content_download_transition(
    content_download_status_t status,
    content_downloader_error_t error
) {
    content_download_transition_t transition = {
        .status = status,
        .error = error,
    };
    return transition;
}

static bool content_download_request_is_valid(
    const content_download_request_t *request
) {
    if (request == NULL || request->package_id[0] == '\0' ||
        request->download_url[0] == '\0' || request->size_bytes == 0 ||
        request->size_bytes > CONTENT_DOWNLOADER_MAX_PACKAGE_BYTES) {
        return false;
    }
    if (strncmp(request->download_url, "https://", 8) != 0) {
        return false;
    }
    if (strlen(request->sha256) != 64) {
        return false;
    }
    for (size_t index = 0; index < 64; ++index) {
        const char value = request->sha256[index];
        const bool digit = value >= '0' && value <= '9';
        const bool lower = value >= 'a' && value <= 'f';
        const bool upper = value >= 'A' && value <= 'F';
        if (!digit && !lower && !upper) {
            return false;
        }
    }
    return true;
}

bool content_download_state_init(
    content_download_state_t *state,
    const content_download_request_t *request,
    int max_attempts
) {
    if (state == NULL || !content_download_request_is_valid(request)) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->request = *request;
    state->status = CONTENT_DOWNLOAD_STATUS_WAITING;
    state->last_error = CONTENT_DOWNLOADER_OK;
    state->total_bytes = request->size_bytes;
    state->max_attempts = max_attempts < 0 ? 0 : max_attempts;
    return true;
}

content_download_transition_t content_download_state_start(
    content_download_state_t *state
) {
    if (state == NULL) {
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_FAILED,
            CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT
        );
    }
    state->canceled = false;
    state->retry_requested = false;
    state->last_error = CONTENT_DOWNLOADER_OK;
    if (state->attempt < state->max_attempts) {
        ++state->attempt;
    }
    state->status = CONTENT_DOWNLOAD_STATUS_DOWNLOADING;
    return content_download_transition(state->status, CONTENT_DOWNLOADER_OK);
}

content_download_range_t content_download_state_range(
    const content_download_state_t *state
) {
    content_download_range_t range = {};
    if (state == NULL || state->received_bytes == 0) {
        return range;
    }
    if (state->received_bytes >= state->request.size_bytes) {
        return range;
    }
    range.use_range = true;
    range.range_start = state->received_bytes;
    range.range_end = state->request.size_bytes - 1;
    return range;
}

content_download_transition_t content_download_state_apply_response(
    content_download_state_t *state,
    int http_status,
    uint64_t requested_offset,
    uint64_t content_range_start,
    uint64_t content_range_end,
    uint64_t content_length
) {
    if (state == NULL) {
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_FAILED,
            CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT
        );
    }
    if (http_status == 206) {
        if (content_range_start != requested_offset ||
            content_range_end >= state->request.size_bytes ||
            content_range_end < content_range_start) {
            return content_download_state_fail(
                state,
                CONTENT_DOWNLOADER_ERR_TRANSPORT
            );
        }
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_DOWNLOADING,
            CONTENT_DOWNLOADER_OK
        );
    }
    if (http_status == 200) {
        if (requested_offset > 0) {
            // The server ignored Range. The caller must truncate before it
            // starts writing, otherwise the resumed prefix would be kept.
            state->received_bytes = 0;
        }
        if (content_length > state->request.size_bytes) {
            return content_download_state_fail(
                state,
                CONTENT_DOWNLOADER_ERR_TOO_LARGE
            );
        }
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_DOWNLOADING,
            CONTENT_DOWNLOADER_OK
        );
    }
    if (http_status == 416 &&
        state->received_bytes == state->request.size_bytes) {
        return content_download_state_begin_verify(state);
    }
    return content_download_state_fail(
        state,
        CONTENT_DOWNLOADER_ERR_HTTP_STATUS
    );
}

content_download_transition_t content_download_state_record_bytes(
    content_download_state_t *state,
    size_t byte_count
) {
    if (state == NULL) {
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_FAILED,
            CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT
        );
    }
    if (byte_count > state->request.size_bytes ||
        state->received_bytes >
            state->request.size_bytes - (uint64_t)byte_count) {
        return content_download_state_fail(
            state,
            CONTENT_DOWNLOADER_ERR_TOO_LARGE
        );
    }
    state->received_bytes += (uint64_t)byte_count;
    return content_download_transition(
        CONTENT_DOWNLOAD_STATUS_DOWNLOADING,
        CONTENT_DOWNLOADER_OK
    );
}

content_download_transition_t content_download_state_begin_verify(
    content_download_state_t *state
) {
    if (state == NULL) {
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_FAILED,
            CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT
        );
    }
    if (state->received_bytes != state->request.size_bytes) {
        return content_download_state_fail(
            state,
            CONTENT_DOWNLOADER_ERR_TRANSPORT
        );
    }
    state->status = CONTENT_DOWNLOAD_STATUS_VERIFYING;
    return content_download_transition(state->status, CONTENT_DOWNLOADER_OK);
}

content_download_transition_t content_download_state_complete(
    content_download_state_t *state
) {
    if (state == NULL) {
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_FAILED,
            CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT
        );
    }
    state->status = CONTENT_DOWNLOAD_STATUS_COMPLETE;
    state->last_error = CONTENT_DOWNLOADER_OK;
    return content_download_transition(state->status, CONTENT_DOWNLOADER_OK);
}

content_download_transition_t content_download_state_fail(
    content_download_state_t *state,
    content_downloader_error_t error
) {
    if (state == NULL) {
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_FAILED,
            CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT
        );
    }
    state->last_error = error;
    if (state->attempt < state->max_attempts &&
        error != CONTENT_DOWNLOADER_ERR_TOO_LARGE &&
        error != CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT) {
        state->status = CONTENT_DOWNLOAD_STATUS_WAITING;
        return content_download_transition(state->status, error);
    }
    state->status = CONTENT_DOWNLOAD_STATUS_FAILED;
    state->last_error = error == CONTENT_DOWNLOADER_OK
        ? CONTENT_DOWNLOADER_ERR_RETRY_EXHAUSTED
        : error;
    return content_download_transition(state->status, state->last_error);
}

content_download_transition_t content_download_state_cancel(
    content_download_state_t *state
) {
    if (state == NULL) {
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_CANCELED,
            CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT
        );
    }
    if (state->status == CONTENT_DOWNLOAD_STATUS_COMPLETE) {
        return content_download_transition(
            CONTENT_DOWNLOAD_STATUS_COMPLETE,
            CONTENT_DOWNLOADER_OK
        );
    }
    state->canceled = true;
    state->status = CONTENT_DOWNLOAD_STATUS_CANCELED;
    return content_download_transition(
        state->status,
        CONTENT_DOWNLOADER_ERR_CANCELED
    );
}

bool content_download_state_is_active(const content_download_state_t *state) {
    if (state == NULL) {
        return false;
    }
    return state->status == CONTENT_DOWNLOAD_STATUS_WAITING ||
        state->status == CONTENT_DOWNLOAD_STATUS_DOWNLOADING ||
        state->status == CONTENT_DOWNLOAD_STATUS_VERIFYING;
}

content_download_progress_t content_download_state_progress(
    const content_download_state_t *state
) {
    content_download_progress_t progress = {};
    if (state == NULL) {
        progress.status = CONTENT_DOWNLOAD_STATUS_IDLE;
        progress.last_error = CONTENT_DOWNLOADER_ERR_NOT_INITIALIZED;
        return progress;
    }
    progress.status = state->status;
    memcpy(
        progress.package_id,
        state->request.package_id,
        sizeof(progress.package_id)
    );
    progress.received_bytes = state->received_bytes;
    progress.total_bytes = state->total_bytes;
    progress.attempt = state->attempt;
    progress.last_error = state->last_error;
    return progress;
}
