#include "ota_download.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_download_core.h"
#include "ota_manifest.h"
#include "psa/crypto.h"

#ifndef CONFIG_OTA_DOWNLOAD_CHUNK_BYTES
#define CONFIG_OTA_DOWNLOAD_CHUNK_BYTES 4096
#endif

#ifndef CONFIG_OTA_DOWNLOAD_TIMEOUT_SECONDS
#define CONFIG_OTA_DOWNLOAD_TIMEOUT_SECONDS 60
#endif

#ifndef CONFIG_OTA_DOWNLOAD_MAX_RETRIES
#define CONFIG_OTA_DOWNLOAD_MAX_RETRIES 3
#endif

#define OTA_DOWNLOAD_MAX_IMAGE_BYTES (8ULL * 1024ULL * 1024ULL)

typedef struct {
    esp_ota_handle_t ota_handle;
    ota_download_state_t state;
    ota_download_progress_callback_t progress_callback;
    void *progress_context;
    ota_download_cancel_callback_t cancel_callback;
    void *cancel_context;
    psa_hash_operation_t hash;
    bool hash_ready;
    bool ota_open;
    uint64_t written_bytes;
    bool range_supported;
} ota_download_session_t;

static bool ota_download_is_canceled(ota_download_session_t *session) {
    if (session == NULL) {
        return true;
    }
    if (session->cancel_callback != NULL &&
        session->cancel_callback(session->cancel_context)) {
        return true;
    }
    return session->state.canceled;
}

static esp_err_t ota_download_hash_start(
    ota_download_session_t *session
) {
    const psa_status_t status = psa_hash_setup(
        &session->hash,
        PSA_ALG_SHA_256
    );
    if (status != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    session->hash_ready = true;
    return ESP_OK;
}

static esp_err_t ota_download_hash_update(
    ota_download_session_t *session,
    const uint8_t *data,
    size_t size
) {
    if (!session->hash_ready || data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return psa_hash_update(&session->hash, data, size) == PSA_SUCCESS
               ? ESP_OK
               : ESP_FAIL;
}

static void ota_download_format_hex(
    const uint8_t *digest,
    size_t digest_size,
    char *output,
    size_t output_size
) {
    static const char hex[] = "0123456789abcdef";
    if (digest == NULL || output == NULL ||
        output_size < digest_size * 2 + 1) {
        return;
    }
    for (size_t index = 0; index < digest_size; ++index) {
        output[index * 2] = hex[(digest[index] >> 4) & 0x0f];
        output[index * 2 + 1] = hex[digest[index] & 0x0f];
    }
    output[digest_size * 2] = '\0';
}

static esp_err_t ota_download_hash_finish(
    ota_download_session_t *session,
    char *output,
    size_t output_size
) {
    if (!session->hash_ready || output == NULL || output_size < 65) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t digest[32] = {0};
    size_t digest_size = 0;
    const psa_status_t status = psa_hash_finish(
        &session->hash,
        digest,
        sizeof(digest),
        &digest_size
    );
    session->hash_ready = false;
    if (status != PSA_SUCCESS || digest_size != sizeof(digest)) {
        memset(digest, 0, sizeof(digest));
        return ESP_FAIL;
    }
    ota_download_format_hex(digest, sizeof(digest), output, output_size);
    memset(digest, 0, sizeof(digest));
    return ESP_OK;
}

static esp_err_t ota_download_write(
    ota_download_session_t *session,
    const uint8_t *data,
    size_t size
) {
    if (session == NULL || data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const ota_download_core_error_t state_result =
        ota_download_state_record_bytes(&session->state, size);
    if (state_result != OTA_DOWNLOAD_CORE_OK) {
        return state_result == OTA_DOWNLOAD_CORE_ERR_CANCELED
                   ? ESP_ERR_INVALID_STATE
                   : ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t hash_result = ota_download_hash_update(
        session,
        data,
        size
    );
    if (hash_result != ESP_OK) {
        return hash_result;
    }
    const esp_err_t write_result = esp_ota_write(
        session->ota_handle,
        data,
        size
    );
    if (write_result != ESP_OK) {
        return write_result;
    }
    session->written_bytes += size;
    if (session->progress_callback != NULL) {
        session->progress_callback(
            session->written_bytes,
            session->state.expected_size,
            session->progress_context
        );
    }
    return ESP_OK;
}

static bool ota_download_parse_content_range(
    const char *value,
    uint64_t *start_out,
    uint64_t *end_out
) {
    if (value == NULL || strncmp(value, "bytes ", 6) != 0) {
        return false;
    }
    char *end_pointer = NULL;
    const unsigned long long start = strtoull(
        value + 6,
        &end_pointer,
        10
    );
    if (end_pointer == value + 6 || *end_pointer != '-') {
        return false;
    }
    const unsigned long long end = strtoull(
        end_pointer + 1,
        &end_pointer,
        10
    );
    if (*end_pointer != '/') {
        return false;
    }
    if (start_out != NULL) {
        *start_out = (uint64_t)start;
    }
    if (end_out != NULL) {
        *end_out = (uint64_t)end;
    }
    return true;
}

static esp_err_t ota_download_perform_attempt(
    ota_download_session_t *session,
    const ota_download_request_t *request,
    bool resume
) {
    esp_http_client_config_t config = {};
    config.url = request->url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = (int)request->timeout_ms;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char range_header[64] = {0};
    if (resume) {
        snprintf(
            range_header,
            sizeof(range_header),
            "bytes=%llu-",
            (unsigned long long)session->written_bytes
        );
        esp_http_client_set_header(client, "Range", range_header);
    }
    esp_http_client_set_header(client, "Accept", "application/octet-stream");

    esp_err_t result = esp_http_client_open(client, 0);
    if (result != ESP_OK) {
        esp_http_client_cleanup(client);
        return result;
    }
    const int content_length = esp_http_client_fetch_headers(client);
    const int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200 && status_code != 206) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint64_t range_start = 0;
    uint64_t range_end = 0;
    char *content_range = NULL;
    (void)esp_http_client_get_header(
        client,
        "Content-Range",
        &content_range
    );
    const bool has_content_range = ota_download_parse_content_range(
        content_range,
        &range_start,
        &range_end
    );
    if (resume) {
        if (status_code != 206 || !has_content_range ||
            range_start != session->written_bytes) {
            // The server ignored Range. Restart from byte zero with a fresh
            // OTA erase so a partial image can never be mistaken for a full
            // one.
            esp_http_client_cleanup(client);
            return ESP_ERR_INVALID_RESPONSE;
        }
        session->range_supported = true;
    } else if (status_code == 206) {
        session->range_supported = true;
    }

    if (content_length <= 0) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }
    const uint64_t advertised_end = has_content_range
        ? range_end + 1
        : (uint64_t)content_length + session->written_bytes;
    if (advertised_end > session->state.expected_size) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t read_buffer[CONFIG_OTA_DOWNLOAD_CHUNK_BYTES] = {0};
    int64_t last_progress_ms = esp_timer_get_time() / 1000;
    while (result == ESP_OK) {
        if (ota_download_is_canceled(session)) {
            ota_download_state_cancel(&session->state);
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        if (session->written_bytes >= session->state.expected_size) {
            break;
        }
        const int read_size = esp_http_client_read(
            client,
            (char *)read_buffer,
            sizeof(read_buffer)
        );
        if (read_size < 0) {
            const int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - last_progress_ms >= (int64_t)request->timeout_ms) {
                result = ESP_ERR_TIMEOUT;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (read_size == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            const int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms - last_progress_ms >= (int64_t)request->timeout_ms) {
                result = ESP_ERR_TIMEOUT;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        result = ota_download_write(
            session,
            read_buffer,
            (size_t)read_size
        );
        last_progress_ms = esp_timer_get_time() / 1000;
    }
    if (result == ESP_OK &&
        session->written_bytes != session->state.expected_size) {
        result = ESP_ERR_INVALID_SIZE;
    }
    esp_http_client_cleanup(client);
    return result;
}

esp_err_t ota_download_to_partition(
    const ota_download_request_t *request,
    const esp_partition_t *partition,
    ota_download_progress_callback_t progress_callback,
    void *progress_context,
    ota_download_cancel_callback_t cancel_callback,
    void *cancel_context
) {
    if (request == NULL || request->url == NULL ||
        request->expected_sha256 == NULL ||
        request->expected_size == 0 || partition == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(request->url, "https://", 8) != 0 ||
        !ota_manifest_sha256_is_valid(request->expected_sha256) ||
        request->expected_size > OTA_DOWNLOAD_MAX_IMAGE_BYTES ||
        request->expected_size > partition->size) {
        return ESP_ERR_INVALID_ARG;
    }

    ota_download_session_t session = {
        .ota_handle = 0,
        .state = {0},
        .progress_callback = progress_callback,
        .progress_context = progress_context,
        .cancel_callback = cancel_callback,
        .cancel_context = cancel_context,
        .hash = PSA_HASH_OPERATION_INIT,
        .hash_ready = false,
        .ota_open = false,
        .written_bytes = 0,
        .range_supported = false,
    };
    ota_download_state_init(
        &session.state,
        request->expected_size,
        request->maximum_retries
    );
    esp_err_t result = ota_download_hash_start(&session);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_ota_begin(
        partition,
        (size_t)request->expected_size,
        &session.ota_handle
    );
    if (result != ESP_OK) {
        (void)psa_hash_abort(&session.hash);
        return result;
    }
    session.ota_open = true;

    bool resume = false;
    while (true) {
        result = ota_download_perform_attempt(&session, request, resume);
        if (result == ESP_OK) {
            break;
        }
        if (session.state.canceled || ota_download_is_canceled(&session)) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        const uint32_t attempts_before_failure = session.state.attempts;
        const ota_download_core_error_t retry_result =
            ota_download_state_record_failure(&session.state);
        const uint32_t attempts_after_failure = session.state.attempts;
        if (retry_result == OTA_DOWNLOAD_CORE_ERR_RETRY_EXHAUSTED) {
            break;
        }
        if (!session.range_supported) {
            // Without Range support a partial partition write cannot be
            // resumed safely. Abort and restart the whole download.
            esp_ota_abort(session.ota_handle);
            session.ota_open = false;
            (void)psa_hash_abort(&session.hash);
            memset(&session.hash, 0, sizeof(session.hash));
            session.written_bytes = 0;
            ota_download_state_init(
                &session.state,
                request->expected_size,
                request->maximum_retries
            );
            session.state.attempts = attempts_before_failure <
                    attempts_after_failure
                ? attempts_after_failure
                : attempts_before_failure;
            esp_err_t restart_result = ota_download_hash_start(&session);
            if (restart_result != ESP_OK) {
                result = restart_result;
                break;
            }
            restart_result = esp_ota_begin(
                partition,
                (size_t)request->expected_size,
                &session.ota_handle
            );
            if (restart_result != ESP_OK) {
                result = restart_result;
                break;
            }
            session.ota_open = true;
            resume = false;
        } else {
            resume = true;
        }
    }

    if (result == ESP_OK) {
        char actual_sha256[65] = {0};
        result = ota_download_hash_finish(
            &session,
            actual_sha256,
            sizeof(actual_sha256)
        );
        if (result == ESP_OK) {
            const ota_download_core_error_t completion =
                ota_download_state_complete(
                    &session.state,
                    request->expected_sha256,
                    actual_sha256
                );
            if (completion != OTA_DOWNLOAD_CORE_OK) {
                result = completion == OTA_DOWNLOAD_CORE_ERR_HASH_MISMATCH
                             ? ESP_ERR_INVALID_CRC
                             : ESP_ERR_INVALID_SIZE;
            }
        }
        memset(actual_sha256, 0, sizeof(actual_sha256));
    }

    if (session.ota_open) {
        if (result == ESP_OK) {
            result = esp_ota_end(session.ota_handle);
        } else {
            (void)esp_ota_abort(session.ota_handle);
        }
        session.ota_open = false;
    }
    if (session.hash_ready) {
        (void)psa_hash_abort(&session.hash);
        session.hash_ready = false;
    }
    return result;
}
