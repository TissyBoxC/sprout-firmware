#include "ota_download_core.h"

#include "ota_manifest.h"

void ota_download_state_init(
    ota_download_state_t *state,
    uint64_t expected_size,
    uint32_t maximum_attempts
) {
    if (state == NULL) {
        return;
    }
    *state = (ota_download_state_t) {
        .expected_size = expected_size,
        .received_size = 0,
        .attempts = 0,
        .maximum_attempts = maximum_attempts == 0 ? 1 : maximum_attempts,
        .canceled = false,
        .complete = false,
        .range_supported = false,
    };
}

ota_download_core_error_t ota_download_state_record_bytes(
    ota_download_state_t *state,
    uint64_t bytes
) {
    if (state == NULL) {
        return OTA_DOWNLOAD_CORE_ERR_INVALID_ARGUMENT;
    }
    if (state->canceled) {
        return OTA_DOWNLOAD_CORE_ERR_CANCELED;
    }
    if (bytes > state->expected_size - state->received_size) {
        state->canceled = true;
        return OTA_DOWNLOAD_CORE_ERR_OVERSIZED;
    }
    state->received_size += bytes;
    return OTA_DOWNLOAD_CORE_OK;
}

ota_download_core_error_t ota_download_state_cancel(
    ota_download_state_t *state
) {
    if (state == NULL) {
        return OTA_DOWNLOAD_CORE_ERR_INVALID_ARGUMENT;
    }
    state->canceled = true;
    return OTA_DOWNLOAD_CORE_OK;
}

ota_download_core_error_t ota_download_state_record_failure(
    ota_download_state_t *state
) {
    if (state == NULL) {
        return OTA_DOWNLOAD_CORE_ERR_INVALID_ARGUMENT;
    }
    if (state->canceled) {
        return OTA_DOWNLOAD_CORE_ERR_CANCELED;
    }
    ++state->attempts;
    if (state->attempts >= state->maximum_attempts) {
        return OTA_DOWNLOAD_CORE_ERR_RETRY_EXHAUSTED;
    }
    return OTA_DOWNLOAD_CORE_OK;
}

bool ota_download_state_should_resume(
    const ota_download_state_t *state
) {
    return state != NULL && state->range_supported &&
        !state->complete && state->received_size > 0 &&
        state->received_size < state->expected_size;
}

uint64_t ota_download_state_resume_offset(
    const ota_download_state_t *state
) {
    return ota_download_state_should_resume(state)
               ? state->received_size
               : 0;
}

ota_download_core_error_t ota_download_state_complete(
    ota_download_state_t *state,
    const char *expected_sha256,
    const char *actual_sha256
) {
    if (state == NULL || expected_sha256 == NULL || actual_sha256 == NULL) {
        return OTA_DOWNLOAD_CORE_ERR_INVALID_ARGUMENT;
    }
    if (state->canceled) {
        return OTA_DOWNLOAD_CORE_ERR_CANCELED;
    }
    if (state->received_size != state->expected_size) {
        return OTA_DOWNLOAD_CORE_ERR_SIZE_MISMATCH;
    }
    if (!ota_manifest_sha256_matches(
            expected_sha256,
            actual_sha256
        )) {
        return OTA_DOWNLOAD_CORE_ERR_HASH_MISMATCH;
    }
    state->complete = true;
    return OTA_DOWNLOAD_CORE_OK;
}
