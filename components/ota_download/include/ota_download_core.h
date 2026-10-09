#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_DOWNLOAD_CORE_OK = 0,
    OTA_DOWNLOAD_CORE_ERR_INVALID_ARGUMENT,
    OTA_DOWNLOAD_CORE_ERR_OVERSIZED,
    OTA_DOWNLOAD_CORE_ERR_HASH_MISMATCH,
    OTA_DOWNLOAD_CORE_ERR_RETRY_EXHAUSTED,
    OTA_DOWNLOAD_CORE_ERR_CANCELED,
    OTA_DOWNLOAD_CORE_ERR_SIZE_MISMATCH,
} ota_download_core_error_t;

typedef struct {
    uint64_t expected_size;
    uint64_t received_size;
    uint32_t attempts;
    uint32_t maximum_attempts;
    bool canceled;
    bool complete;
    bool range_supported;
} ota_download_state_t;

void ota_download_state_init(
    ota_download_state_t *state,
    uint64_t expected_size,
    uint32_t maximum_attempts
);

ota_download_core_error_t ota_download_state_record_bytes(
    ota_download_state_t *state,
    uint64_t bytes
);

ota_download_core_error_t ota_download_state_cancel(
    ota_download_state_t *state
);

ota_download_core_error_t ota_download_state_record_failure(
    ota_download_state_t *state
);

bool ota_download_state_should_resume(
    const ota_download_state_t *state
);

uint64_t ota_download_state_resume_offset(
    const ota_download_state_t *state
);

ota_download_core_error_t ota_download_state_complete(
    ota_download_state_t *state,
    const char *expected_sha256,
    const char *actual_sha256
);

#ifdef __cplusplus
}
#endif
