#include "content_downloader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#include "content_download_state.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "network_manager.h"

#ifndef CONFIG_CONTENT_DOWNLOADER_TIMEOUT_SECONDS
#define CONFIG_CONTENT_DOWNLOADER_TIMEOUT_SECONDS 60
#endif

#ifndef CONFIG_CONTENT_DOWNLOADER_MAX_RETRIES
#define CONFIG_CONTENT_DOWNLOADER_MAX_RETRIES 3
#endif

#ifndef CONFIG_CONTENT_DOWNLOADER_CHUNK_BYTES
#define CONFIG_CONTENT_DOWNLOADER_CHUNK_BYTES 8192
#endif

#define CONTENT_DOWNLOADER_QUEUE_CAPACITY 8
#define CONTENT_DOWNLOADER_PATH_SIZE 192
#define CONTENT_DOWNLOADER_OFFLINE_RETRY_MS 1500

static const char *const TAG = "content_downloader";

typedef struct {
    bool used;
    content_download_state_t state;
} content_download_slot_t;

static bool content_downloader_ready;
static content_download_slot_t content_downloader_slots[
    CONTENT_DOWNLOADER_QUEUE_CAPACITY
];
static SemaphoreHandle_t content_downloader_lock;
static TaskHandle_t content_downloader_task_handle;
static volatile bool content_downloader_worker_running;

/* Defined in content_playback.c and owned by the downloader feature flag. */
esp_err_t content_playback_init(void);

static bool content_downloader_is_canceled(
    const content_download_slot_t *slot
) {
    bool canceled = false;
    xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
    canceled = slot != NULL && slot->state.canceled;
    xSemaphoreGive(content_downloader_lock);
    return canceled;
}

static void content_downloader_make_paths(
    const char *package_id,
    char *final_path,
    size_t final_size,
    char *temporary_path,
    size_t temporary_size
) {
    snprintf(
        final_path,
        final_size,
        "%s/%s.pkg",
        CONTENT_DOWNLOADER_MOUNT_POINT,
        package_id
    );
    snprintf(
        temporary_path,
        temporary_size,
        "%s/%s.part",
        CONTENT_DOWNLOADER_MOUNT_POINT,
        package_id
    );
}

static content_download_slot_t *content_downloader_find_locked(
    const char *package_id
) {
    for (size_t index = 0; index < CONTENT_DOWNLOADER_QUEUE_CAPACITY; ++index) {
        if (content_downloader_slots[index].used &&
            strcmp(
                content_downloader_slots[index].state.request.package_id,
                package_id
            ) == 0) {
            return &content_downloader_slots[index];
        }
    }
    return NULL;
}

static content_download_slot_t *content_downloader_next_locked(void) {
    for (size_t index = 0; index < CONTENT_DOWNLOADER_QUEUE_CAPACITY; ++index) {
        content_download_slot_t *slot = &content_downloader_slots[index];
        if (slot->used &&
            slot->state.status == CONTENT_DOWNLOAD_STATUS_WAITING) {
            return slot;
        }
    }
    return NULL;
}

static esp_err_t content_downloader_remove_path(const char *path) {
    if (remove(path) == 0) {
        return ESP_OK;
    }
    return errno == ENOENT ? ESP_OK : ESP_FAIL;
}

static uint64_t content_downloader_temp_size(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    const long size = ftell(file);
    fclose(file);
    return size > 0 ? (uint64_t)size : 0;
}

static void content_downloader_format_hex(
    const uint8_t *digest,
    size_t digest_size,
    char *output,
    size_t output_size
) {
    static const char hex[] = "0123456789abcdef";
    const size_t required = digest_size * 2 + 1;
    if (output == NULL || output_size < required) {
        return;
    }
    for (size_t index = 0; index < digest_size; ++index) {
        output[index * 2] = hex[(digest[index] >> 4) & 0x0f];
        output[index * 2 + 1] = hex[digest[index] & 0x0f];
    }
    output[digest_size * 2] = '\0';
}

static esp_err_t content_downloader_hash_file(
    const char *path,
    char *hex_out,
    size_t hex_out_size
) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    psa_status_t result = psa_crypto_init();
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    if (result == PSA_SUCCESS) {
        result = psa_hash_setup(&operation, PSA_ALG_SHA_256);
    }
    uint8_t buffer[1024];
    while (result == PSA_SUCCESS) {
        const size_t read_size = fread(buffer, 1, sizeof(buffer), file);
        if (read_size == 0) {
            break;
        }
        result = psa_hash_update(&operation, buffer, read_size);
    }
    uint8_t digest[32] = {0};
    size_t digest_size = 0;
    if (result == PSA_SUCCESS) {
        result = psa_hash_finish(
            &operation,
            digest,
            sizeof(digest),
            &digest_size
        );
    }
    (void)psa_hash_abort(&operation);
    fclose(file);
    if (result != PSA_SUCCESS || digest_size != sizeof(digest)) {
        return ESP_FAIL;
    }
    content_downloader_format_hex(digest, sizeof(digest), hex_out, hex_out_size);
    memset(digest, 0, sizeof(digest));
    return ESP_OK;
}

static esp_err_t content_downloader_verify(
    content_download_slot_t *slot,
    const char *temporary_path
) {
    char actual_sha256[65] = {0};
    const esp_err_t hash_result = content_downloader_hash_file(
        temporary_path,
        actual_sha256,
        sizeof(actual_sha256)
    );
    if (hash_result != ESP_OK) {
        return hash_result;
    }
    const bool matches = strcasecmp(
        actual_sha256,
        slot->state.request.sha256
    ) == 0;
    memset(actual_sha256, 0, sizeof(actual_sha256));
    return matches ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static bool content_downloader_parse_content_range(
    const char *header_value,
    uint64_t *start_out,
    uint64_t *end_out
) {
    if (header_value == NULL || strncmp(header_value, "bytes ", 6) != 0) {
        return false;
    }
    const char *dash = strchr(header_value + 6, '-');
    if (dash == NULL) {
        return false;
    }
    char *end_pointer = NULL;
    const unsigned long long start = strtoull(
        header_value + 6,
        &end_pointer,
        10
    );
    if (end_pointer != dash) {
        return false;
    }
    const unsigned long long end = strtoull(dash + 1, &end_pointer, 10);
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

typedef struct {
    FILE *file;
    content_download_state_t *state;
    uint64_t offset;
    bool failed;
    content_downloader_error_t error;
} content_downloader_stream_t;

static esp_err_t content_downloader_write_stream_data(
    content_downloader_stream_t *stream,
    const char *data,
    size_t data_size
) {
    if (stream->state->canceled) {
        stream->failed = true;
        stream->error = CONTENT_DOWNLOADER_ERR_CANCELED;
        return ESP_FAIL;
    }
    const content_download_transition_t transition =
        content_download_state_record_bytes(stream->state, data_size);
    if (transition.error != CONTENT_DOWNLOADER_OK) {
        stream->failed = true;
        stream->error = transition.error;
        return ESP_FAIL;
    }
    const size_t written = fwrite(data, 1, data_size, stream->file);
    if (written != data_size) {
        stream->failed = true;
        stream->error = CONTENT_DOWNLOADER_ERR_STORAGE;
        return ESP_FAIL;
    }
    stream->offset += (uint64_t)data_size;
    return ESP_OK;
}

static content_download_slot_t *content_downloader_active_slot;

static bool content_downloader_stream_canceled(void) {
    content_download_slot_t *slot = content_downloader_active_slot;
    return content_downloader_is_canceled(slot);
}

static esp_err_t content_downloader_perform_once(
    content_download_slot_t *slot
) {
    char final_path[CONTENT_DOWNLOADER_PATH_SIZE] = {0};
    char temporary_path[CONTENT_DOWNLOADER_PATH_SIZE] = {0};
    content_downloader_make_paths(
        slot->state.request.package_id,
        final_path,
        sizeof(final_path),
        temporary_path,
        sizeof(temporary_path)
    );
    (void)final_path;

    uint64_t existing_size = content_downloader_temp_size(temporary_path);
    if (existing_size > slot->state.request.size_bytes) {
        (void)content_downloader_remove_path(temporary_path);
        existing_size = 0;
    }
    slot->state.received_bytes = existing_size;
    content_download_range_t range =
        content_download_state_range(&slot->state);
    uint64_t offset = range.use_range ? range.range_start : 0;
    bool truncate = !range.use_range || offset == 0;
    if (truncate) {
        slot->state.received_bytes = 0;
    }

    FILE *output = fopen(temporary_path, truncate ? "wb" : "r+b");
    if (output == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!truncate && fseek(output, (long)offset, SEEK_SET) != 0) {
        fclose(output);
        return ESP_FAIL;
    }
    content_downloader_stream_t stream = {
        .file = output,
        .state = &slot->state,
        .offset = offset,
        .failed = false,
        .error = CONTENT_DOWNLOADER_OK,
    };
    esp_http_client_config_t config = {};
    config.url = slot->state.request.download_url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = CONFIG_CONTENT_DOWNLOADER_TIMEOUT_SECONDS * 1000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        fclose(output);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    if (range.use_range) {
        char range_header[64] = {0};
        snprintf(
            range_header,
            sizeof(range_header),
            "bytes=%llu-%llu",
            (unsigned long long)range.range_start,
            (unsigned long long)range.range_end
        );
        esp_http_client_set_header(client, "Range", range_header);
    }

    esp_err_t result = esp_http_client_open(client, 0);
    int64_t last_progress_at_ms = esp_timer_get_time() / 1000;
    if (result == ESP_OK) {
        const int content_length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (content_length < 0) {
            result = ESP_FAIL;
        } else {
            uint64_t range_start = 0;
            uint64_t range_end = 0;
            char *content_range = NULL;
            (void)esp_http_client_get_header(
                client,
                "Content-Range",
                &content_range
            );
            const bool has_content_range =
                content_downloader_parse_content_range(
                    content_range,
                    &range_start,
                    &range_end
                );
            const content_download_transition_t response_transition =
                content_download_state_apply_response(
                    &slot->state,
                    status,
                    offset,
                    has_content_range ? range_start : 0,
                    has_content_range ? range_end : 0,
                    (uint64_t)content_length
                );
            if (response_transition.error != CONTENT_DOWNLOADER_OK) {
                result = ESP_ERR_INVALID_RESPONSE;
            } else {
                if (status == 200 && offset > 0) {
                    if (fseek(output, 0, SEEK_SET) != 0 ||
                        ftruncate(fileno(output), 0) != 0) {
                        result = ESP_FAIL;
                    } else {
                        stream.offset = 0;
                    }
                }
                uint8_t read_buffer[CONFIG_CONTENT_DOWNLOADER_CHUNK_BYTES];
                while (result == ESP_OK && !stream.failed) {
                    if (content_downloader_stream_canceled()) {
                        stream.failed = true;
                        stream.error = CONTENT_DOWNLOADER_ERR_CANCELED;
                        result = ESP_FAIL;
                        break;
                    }
                    const int read_size = esp_http_client_read(
                        client,
                        (char *)read_buffer,
                        sizeof(read_buffer)
                    );
                    if (read_size < 0) {
                        const int64_t elapsed_ms =
                            esp_timer_get_time() / 1000 - last_progress_at_ms;
                        if (elapsed_ms >=
                            CONFIG_CONTENT_DOWNLOADER_TIMEOUT_SECONDS * 1000) {
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
                        const int64_t elapsed_ms =
                            esp_timer_get_time() / 1000 - last_progress_at_ms;
                        if (elapsed_ms >=
                            CONFIG_CONTENT_DOWNLOADER_TIMEOUT_SECONDS * 1000) {
                            result = ESP_ERR_TIMEOUT;
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(20));
                        continue;
                    }
                    result = content_downloader_write_stream_data(
                        &stream,
                        (const char *)read_buffer,
                        (size_t)read_size
                    );
                    last_progress_at_ms = esp_timer_get_time() / 1000;
                    if (stream.failed) {
                        result = ESP_FAIL;
                        break;
                    }
                }
                if (result == ESP_OK && !stream.failed &&
                    !esp_http_client_is_complete_data_received(client)) {
                    result = ESP_ERR_INVALID_RESPONSE;
                }
            }
        }
    }
    if (result != ESP_OK && !stream.failed) {
        stream.failed = true;
        stream.error = CONTENT_DOWNLOADER_ERR_TRANSPORT;
    }
    const int flush_result = fflush(output);
    fclose(output);
    esp_http_client_cleanup(client);
    if (flush_result != 0) {
        return ESP_FAIL;
    }
    return result == ESP_OK && !stream.failed ? ESP_OK : ESP_FAIL;
}

static esp_err_t content_downloader_mount(void) {
    esp_vfs_spiffs_conf_t config = {
        .base_path = CONTENT_DOWNLOADER_MOUNT_POINT,
        .partition_label = CONTENT_DOWNLOADER_PARTITION_LABEL,
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    const esp_err_t result = esp_vfs_spiffs_register(&config);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    const esp_err_t info_result = esp_spiffs_info(
        CONTENT_DOWNLOADER_PARTITION_LABEL,
        &total_bytes,
        &used_bytes
    );
    if (info_result != ESP_OK) {
        return info_result;
    }
    ESP_LOGI(
        TAG,
        "content partition mounted: total=%u used=%u",
        (unsigned int)total_bytes,
        (unsigned int)used_bytes
    );
    return ESP_OK;
}

static void content_downloader_finish_slot(
    content_download_slot_t *slot
) {
    char final_path[CONTENT_DOWNLOADER_PATH_SIZE] = {0};
    char temporary_path[CONTENT_DOWNLOADER_PATH_SIZE] = {0};
    content_downloader_make_paths(
        slot->state.request.package_id,
        final_path,
        sizeof(final_path),
        temporary_path,
        sizeof(temporary_path)
    );
    if (slot->state.status == CONTENT_DOWNLOAD_STATUS_CANCELED) {
        (void)content_downloader_remove_path(temporary_path);
        return;
    }
    if (slot->state.status == CONTENT_DOWNLOAD_STATUS_FAILED) {
        (void)content_library_update_state(
            slot->state.request.package_id,
            CONTENT_LIBRARY_STATE_FAILED
        );
        return;
    }
    if (slot->state.status != CONTENT_DOWNLOAD_STATUS_VERIFYING) {
        return;
    }
    if (content_downloader_verify(slot, temporary_path) != ESP_OK) {
        (void)content_downloader_remove_path(temporary_path);
        (void)content_download_state_fail(
            &slot->state,
            CONTENT_DOWNLOADER_ERR_HASH_MISMATCH
        );
        (void)content_library_update_state(
            slot->state.request.package_id,
            CONTENT_LIBRARY_STATE_FAILED
        );
        return;
    }
    if (rename(temporary_path, final_path) != 0) {
        (void)content_download_state_fail(
            &slot->state,
            CONTENT_DOWNLOADER_ERR_STORAGE
        );
        (void)content_library_update_state(
            slot->state.request.package_id,
            CONTENT_LIBRARY_STATE_FAILED
        );
        return;
    }
    (void)content_download_state_complete(&slot->state);
    (void)content_library_update_state(
        slot->state.request.package_id,
        CONTENT_LIBRARY_STATE_READY
    );
}

static void content_downloader_worker(void *argument) {
    (void)argument;
    while (content_downloader_worker_running) {
        xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
        content_download_slot_t *slot = content_downloader_next_locked();
        if (slot == NULL) {
            content_downloader_active_slot = NULL;
            xSemaphoreGive(content_downloader_lock);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        content_downloader_active_slot = slot;
        xSemaphoreGive(content_downloader_lock);
        (void)content_library_update_state(
            slot->state.request.package_id,
            CONTENT_LIBRARY_STATE_DOWNLOADING
        );
        while (true) {
            xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
            if (slot->state.canceled) {
                (void)content_download_state_cancel(&slot->state);
                xSemaphoreGive(content_downloader_lock);
                break;
            }
            (void)content_download_state_start(&slot->state);
            if (network_manager_get_state() !=
                NETWORK_MANAGER_STATE_CONNECTED) {
                const content_download_transition_t transition =
                    content_download_state_fail(
                        &slot->state,
                        CONTENT_DOWNLOADER_ERR_OFFLINE
                    );
                if (transition.status != CONTENT_DOWNLOAD_STATUS_WAITING) {
                    xSemaphoreGive(content_downloader_lock);
                    break;
                }
                xSemaphoreGive(content_downloader_lock);
                vTaskDelay(
                    pdMS_TO_TICKS(CONTENT_DOWNLOADER_OFFLINE_RETRY_MS)
                );
                continue;
            }
            xSemaphoreGive(content_downloader_lock);
            const esp_err_t transfer_result =
                content_downloader_perform_once(slot);
            xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
            if (transfer_result != ESP_OK) {
                const bool canceled = slot->state.canceled;
                const content_download_transition_t transition =
                    canceled
                        ? content_download_state_cancel(&slot->state)
                        : content_download_state_fail(
                            &slot->state,
                            CONTENT_DOWNLOADER_ERR_TRANSPORT
                        );
                if (transition.status != CONTENT_DOWNLOAD_STATUS_WAITING) {
                    xSemaphoreGive(content_downloader_lock);
                    break;
                }
                xSemaphoreGive(content_downloader_lock);
                continue;
            }
            (void)content_download_state_begin_verify(&slot->state);
            xSemaphoreGive(content_downloader_lock);
            content_downloader_finish_slot(slot);
            break;
        }
        content_downloader_active_slot = NULL;
        if (slot->state.status == CONTENT_DOWNLOAD_STATUS_CANCELED) {
            xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
            memset(slot, 0, sizeof(*slot));
            xSemaphoreGive(content_downloader_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    content_downloader_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t content_downloader_init(void) {
    if (content_downloader_ready) {
        return ESP_OK;
    }
    if (!content_library_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    content_downloader_lock = xSemaphoreCreateMutex();
    if (content_downloader_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(
        content_downloader_slots,
        0,
        sizeof(content_downloader_slots)
    );
    if (content_downloader_mount() != ESP_OK) {
        vSemaphoreDelete(content_downloader_lock);
        content_downloader_lock = NULL;
        return ESP_ERR_INVALID_STATE;
    }
    if (content_playback_init() != ESP_OK) {
        vSemaphoreDelete(content_downloader_lock);
        content_downloader_lock = NULL;
        return ESP_ERR_INVALID_STATE;
    }
    content_downloader_worker_running = true;
    if (xTaskCreate(
            content_downloader_worker,
            "sprout_content",
            6144,
            NULL,
            2,
            &content_downloader_task_handle
        ) != pdPASS) {
        content_downloader_worker_running = false;
        vSemaphoreDelete(content_downloader_lock);
        content_downloader_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    content_downloader_ready = true;
    return ESP_OK;
}

bool content_downloader_is_ready(void) {
    return content_downloader_ready;
}

esp_err_t content_downloader_enqueue(
    const content_download_request_t *request
) {
    if (!content_downloader_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (request == NULL || request->package_id[0] == '\0' ||
        request->download_url[0] == '\0' ||
        request->size_bytes == 0 ||
        request->size_bytes > CONTENT_DOWNLOADER_MAX_PACKAGE_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
    if (content_downloader_find_locked(request->package_id) != NULL) {
        xSemaphoreGive(content_downloader_lock);
        return ESP_ERR_INVALID_STATE;
    }
    content_download_slot_t *slot = NULL;
    for (size_t index = 0; index < CONTENT_DOWNLOADER_QUEUE_CAPACITY; ++index) {
        if (!content_downloader_slots[index].used) {
            slot = &content_downloader_slots[index];
            break;
        }
    }
    if (slot == NULL) {
        xSemaphoreGive(content_downloader_lock);
        return ESP_ERR_NO_MEM;
    }
    if (!content_download_state_init(
            &slot->state,
            request,
            CONFIG_CONTENT_DOWNLOADER_MAX_RETRIES + 1
        )) {
        xSemaphoreGive(content_downloader_lock);
        return ESP_ERR_INVALID_ARG;
    }
    slot->used = true;
    xSemaphoreGive(content_downloader_lock);
    return ESP_OK;
}

esp_err_t content_downloader_cancel(const char *package_id) {
    if (!content_downloader_ready || package_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
    content_download_slot_t *slot = content_downloader_find_locked(package_id);
    if (slot != NULL) {
        (void)content_download_state_cancel(&slot->state);
    }
    xSemaphoreGive(content_downloader_lock);
    return slot != NULL ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t content_downloader_retry(const char *package_id) {
    if (!content_downloader_ready || package_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
    content_download_slot_t *slot = content_downloader_find_locked(package_id);
    if (slot != NULL &&
        (slot->state.status == CONTENT_DOWNLOAD_STATUS_FAILED ||
         slot->state.status == CONTENT_DOWNLOAD_STATUS_CANCELED)) {
        slot->state.status = CONTENT_DOWNLOAD_STATUS_WAITING;
        slot->state.retry_requested = true;
        slot->state.last_error = CONTENT_DOWNLOADER_OK;
    }
    xSemaphoreGive(content_downloader_lock);
    return slot != NULL ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t content_downloader_get_progress(
    const char *package_id,
    content_download_progress_t *progress_out
) {
    if (!content_downloader_ready || package_id == NULL ||
        progress_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(content_downloader_lock, portMAX_DELAY);
    content_download_slot_t *slot = content_downloader_find_locked(package_id);
    if (slot != NULL) {
        *progress_out = content_download_state_progress(&slot->state);
    }
    xSemaphoreGive(content_downloader_lock);
    return slot != NULL ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t content_downloader_get_local_path(
    const char *package_id,
    char *output,
    size_t output_size
) {
    if (!content_downloader_ready || package_id == NULL || output == NULL ||
        output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[CONTENT_DOWNLOADER_PATH_SIZE] = {0};
    char ignored_path[CONTENT_DOWNLOADER_PATH_SIZE] = {0};
    content_downloader_make_paths(
        package_id,
        path,
        sizeof(path),
        ignored_path,
        sizeof(ignored_path)
    );
    if (strlen(path) + 1 > output_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(output, path, strlen(path) + 1);
    return ESP_OK;
}

esp_err_t content_downloader_delete(const char *package_id) {
    if (!content_downloader_ready || package_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char final_path[CONTENT_DOWNLOADER_PATH_SIZE] = {0};
    char temporary_path[CONTENT_DOWNLOADER_PATH_SIZE] = {0};
    content_downloader_make_paths(
        package_id,
        final_path,
        sizeof(final_path),
        temporary_path,
        sizeof(temporary_path)
    );
    const esp_err_t final_result = content_downloader_remove_path(final_path);
    const esp_err_t temporary_result =
        content_downloader_remove_path(temporary_path);
    return final_result != ESP_OK ? final_result : temporary_result;
}

const char *content_downloader_error_name(content_downloader_error_t error) {
    switch (error) {
        case CONTENT_DOWNLOADER_OK:
            return "ok";
        case CONTENT_DOWNLOADER_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case CONTENT_DOWNLOADER_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case CONTENT_DOWNLOADER_ERR_OFFLINE:
            return "offline";
        case CONTENT_DOWNLOADER_ERR_TRANSPORT:
            return "transport";
        case CONTENT_DOWNLOADER_ERR_HTTP_STATUS:
            return "http_status";
        case CONTENT_DOWNLOADER_ERR_TOO_LARGE:
            return "too_large";
        case CONTENT_DOWNLOADER_ERR_STORAGE:
            return "storage";
        case CONTENT_DOWNLOADER_ERR_HASH_MISMATCH:
            return "hash_mismatch";
        case CONTENT_DOWNLOADER_ERR_CANCELED:
            return "canceled";
        case CONTENT_DOWNLOADER_ERR_RETRY_EXHAUSTED:
            return "retry_exhausted";
        default:
            return "unknown";
    }
}

const char *content_download_status_name(content_download_status_t status) {
    switch (status) {
        case CONTENT_DOWNLOAD_STATUS_IDLE:
            return "idle";
        case CONTENT_DOWNLOAD_STATUS_WAITING:
            return "waiting";
        case CONTENT_DOWNLOAD_STATUS_DOWNLOADING:
            return "downloading";
        case CONTENT_DOWNLOAD_STATUS_VERIFYING:
            return "verifying";
        case CONTENT_DOWNLOAD_STATUS_COMPLETE:
            return "complete";
        case CONTENT_DOWNLOAD_STATUS_FAILED:
            return "failed";
        case CONTENT_DOWNLOAD_STATUS_CANCELED:
            return "canceled";
        default:
            return "unknown";
    }
}

const module_descriptor_t *content_downloader_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "content_downloader",
        .version = "1.0.0",
        .initialize = content_downloader_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
