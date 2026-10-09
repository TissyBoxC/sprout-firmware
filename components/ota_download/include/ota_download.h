#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ota_download_progress_callback_t)(
    uint64_t received_bytes,
    uint64_t total_bytes,
    void *context
);

typedef bool (*ota_download_cancel_callback_t)(void *context);

typedef struct {
    const char *url;
    const char *expected_sha256;
    uint64_t expected_size;
    uint32_t maximum_retries;
    uint32_t timeout_ms;
} ota_download_request_t;

/**
 * @brief Download an image into the next OTA partition.
 *
 * The stream is written directly to the inactive partition while SHA-256 is
 * calculated incrementally. Transient transport failures are retried with
 * HTTP Range when the server supports it. The caller remains responsible for
 * calling ota_validate_partition and esp_ota_set_boot_partition.
 */
esp_err_t ota_download_to_partition(
    const ota_download_request_t *request,
    const esp_partition_t *partition,
    ota_download_progress_callback_t progress_callback,
    void *progress_context,
    ota_download_cancel_callback_t cancel_callback,
    void *cancel_context
);

#ifdef __cplusplus
}
#endif
