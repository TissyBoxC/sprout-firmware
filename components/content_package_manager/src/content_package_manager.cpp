#include "content_package_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ArduinoJson.h"

#include "content_downloader.h"
#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "time_sync.h"

extern "C" {
#include "content_package_state.h"
}

#ifndef CONFIG_CONTENT_PACKAGE_SYNC_INTERVAL_SECONDS
#define CONFIG_CONTENT_PACKAGE_SYNC_INTERVAL_SECONDS 900
#endif

#define CONTENT_PACKAGE_URL_SIZE 384
#define CONTENT_PACKAGE_RESPONSE_SIZE 16384
#define CONTENT_PACKAGE_MANIFEST_LIMIT 128
#define CONTENT_PACKAGE_DOWNLOAD_RESPONSE_SIZE 4096
#define CONTENT_PACKAGE_JSON_LIMIT 32768
#define CONTENT_PACKAGE_REVISION_KEY "content_catalog_revision"

static const char *const TAG = "content_manager";

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} content_package_response_t;

static bool content_package_manager_ready;
static content_package_runtime_t content_package_runtime;
static SemaphoreHandle_t content_package_lock;
static TaskHandle_t content_package_task_handle;
static volatile bool content_package_stop;
static int64_t content_package_catalog_revision;

static esp_err_t content_package_response_init(
    content_package_response_t *response,
    size_t capacity
) {
    response->body = (char *)malloc(capacity);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = capacity;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static int64_t content_package_load_revision(void) {
    int64_t revision = 0;
    size_t value_size = sizeof(revision);
    if (config_store_get_blob(
            CONTENT_PACKAGE_REVISION_KEY,
            &revision,
            &value_size
        ) != ESP_OK ||
        value_size != sizeof(revision) ||
        revision < 0) {
        return 0;
    }
    return revision;
}

static esp_err_t content_package_save_revision(int64_t revision) {
    if (revision < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return config_store_set_blob(
        CONTENT_PACKAGE_REVISION_KEY,
        &revision,
        sizeof(revision)
    );
}

static void content_package_response_destroy(
    content_package_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t content_package_http_event(
    esp_http_client_event_t *event
) {
    content_package_response_t *response =
        (content_package_response_t *)event->user_data;
    if (response == NULL || event->event_id != HTTP_EVENT_ON_DATA ||
        event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t remaining = response->length + 1 < response->capacity
        ? response->capacity - 1 - response->length
        : 0;
    const size_t copy_size = (size_t)event->data_len < remaining
        ? (size_t)event->data_len
        : remaining;
    if (copy_size < (size_t)event->data_len) {
        response->overflowed = true;
    }
    memcpy(response->body + response->length, event->data, copy_size);
    response->length += copy_size;
    response->body[response->length] = '\0';
    return ESP_OK;
}

static content_package_error_t content_package_map_http(
    int status_code,
    esp_err_t transport_result
) {
    if (transport_result != ESP_OK) {
        return CONTENT_PACKAGE_ERR_OFFLINE;
    }
    if (status_code == 401 || status_code == 403) {
        return CONTENT_PACKAGE_ERR_UNAUTHENTICATED;
    }
    if (status_code < 200 || status_code >= 300) {
        return CONTENT_PACKAGE_ERR_MANIFEST;
    }
    return CONTENT_PACKAGE_OK;
}

static content_package_error_t content_package_fetch_document(
    const char *path,
    size_t response_capacity,
    JsonDocument *document
) {
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        return CONTENT_PACKAGE_ERR_OFFLINE;
    }
    if (!time_sync_is_synchronized()) {
        return CONTENT_PACKAGE_ERR_OFFLINE;
    }
    if (!device_binding_client_is_registered()) {
        return CONTENT_PACKAGE_ERR_UNAUTHENTICATED;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    if (device_binding_client_copy_session_token(
            session_token,
            sizeof(session_token)
        ) != ESP_OK) {
        return CONTENT_PACKAGE_ERR_UNAUTHENTICATED;
    }
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    if (device_identity_copy(device_id, sizeof(device_id)) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return CONTENT_PACKAGE_ERR_UNAUTHENTICATED;
    }
    char base_url[CONTENT_PACKAGE_URL_SIZE] = {0};
    if (device_binding_client_get_platform_base_url(
            base_url,
            sizeof(base_url)
        ) != ESP_OK ||
        strncmp(base_url, "https://", 8) != 0) {
        memset(session_token, 0, sizeof(session_token));
        return CONTENT_PACKAGE_ERR_MANIFEST;
    }
    char url[CONTENT_PACKAGE_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/content%s",
        base_url,
        device_id,
        path
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return CONTENT_PACKAGE_ERR_MANIFEST;
    }

    content_package_response_t response = {};
    if (content_package_response_init(&response, response_capacity) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return CONTENT_PACKAGE_ERR_STORAGE;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = content_package_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        content_package_response_destroy(&response);
        memset(session_token, 0, sizeof(session_token));
        return CONTENT_PACKAGE_ERR_STORAGE;
    }
    char authorization[DEVICE_BINDING_SESSION_TOKEN_SIZE + 16] = {0};
    snprintf(
        authorization,
        sizeof(authorization),
        "Bearer %s",
        session_token
    );
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Accept", "application/json");
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    memset(session_token, 0, sizeof(session_token));
    const content_package_error_t error = content_package_map_http(
        status_code,
        transport_result
    );
    if (error != CONTENT_PACKAGE_OK) {
        content_package_response_destroy(&response);
        return error;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        content_package_response_destroy(&response);
        return CONTENT_PACKAGE_ERR_MANIFEST;
    }
    content_package_response_destroy(&response);
    return CONTENT_PACKAGE_OK;
}

static content_package_error_t content_package_fetch_manifest(
    JsonDocument *document
) {
    char path[128] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/catalog?since_revision=%lld",
        (long long)content_package_catalog_revision
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        return CONTENT_PACKAGE_ERR_MANIFEST;
    }
    return content_package_fetch_document(
        path,
        CONTENT_PACKAGE_RESPONSE_SIZE,
        document
    );
}

static bool content_package_parse_item(
    JsonVariantConst value,
    content_library_manifest_item_t *item
) {
    JsonVariantConst entry_value = value;
    if (!entry_value.is<JsonObjectConst>()) {
        return false;
    }
    content_library_entry_t entry = {};
    const char *package_id = entry_value["package_id"] | "";
    const char *title = entry_value["title"] | "";
    const char *category = entry_value["category"] | "";
    const char *asset_key = entry_value["asset_key"] | "";
    const char *sha256 = entry_value["sha256"] | "";
    if (package_id[0] == '\0' || category[0] == '\0' ||
        asset_key[0] == '\0' || strlen(sha256) != 64) {
        return false;
    }
    snprintf(
        entry.package_id,
        sizeof(entry.package_id),
        "%s",
        package_id
    );
    entry.package_version = entry_value["package_version"] | 0u;
    snprintf(entry.title, sizeof(entry.title), "%s", title);
    snprintf(entry.category, sizeof(entry.category), "%s", category);
    snprintf(entry.asset_key, sizeof(entry.asset_key), "%s", asset_key);
    snprintf(entry.sha256, sizeof(entry.sha256), "%s", sha256);
    entry.size_bytes = entry_value["size_bytes"] | 0ULL;
    const char *published_at = entry_value["published_at"] | "";
    entry.published_at = 0;
    if (published_at[0] != '\0') {
        struct tm parsed_time = {};
        char *parsed_end = strptime(
            published_at,
            "%Y-%m-%dT%H:%M:%S",
            &parsed_time
        );
        if (parsed_end != NULL) {
            entry.published_at = (int64_t)timegm(&parsed_time);
        }
    }
    const char *local_path = entry_value["local_path"] | "";
    snprintf(
        entry.local_path,
        sizeof(entry.local_path),
        "%s",
        local_path
    );
    entry.state = CONTENT_LIBRARY_STATE_MISSING;
    JsonArrayConst age_tiers =
        entry_value["age_tiers"].as<JsonArrayConst>();
    for (JsonVariantConst tier : age_tiers) {
        if (tier.is<int>()) {
            const int index = tier.as<int>();
            if (index >= 0 && index < CONTENT_LIBRARY_AGE_TIER_COUNT) {
                entry.age_tiers[index] = true;
            }
            continue;
        }
        const char *age_tier = tier | "";
        if (strcmp(age_tier, "age_3_4") == 0) {
            entry.age_tiers[0] = true;
        } else if (strcmp(age_tier, "age_5_6") == 0) {
            entry.age_tiers[1] = true;
        } else if (strcmp(age_tier, "age_7_8") == 0) {
            entry.age_tiers[2] = true;
        }
    }
    if (!entry.age_tiers[0] && !entry.age_tiers[1] &&
        !entry.age_tiers[2] && !entry.age_tiers[3]) {
        entry.age_tiers[0] = true;
    }
    item->operation = CONTENT_LIBRARY_OPERATION_UPSERT;
    item->entry = entry;
    return true;
}

static content_package_error_t content_package_apply_manifest(
    JsonDocument &document,
    bool *all_downloads_queued_out
) {
    if (all_downloads_queued_out != NULL) {
        *all_downloads_queued_out = true;
    }
    JsonVariantConst data = document["data"];
    JsonArrayConst items = data["packages"].as<JsonArrayConst>();
    if (items.isNull()) {
        return CONTENT_PACKAGE_ERR_MANIFEST;
    }
    size_t applied = 0;
    content_library_manifest_item_t parsed[CONTENT_PACKAGE_MANIFEST_LIMIT];
    size_t parsed_count = 0;
    for (JsonVariantConst value : items) {
        if (parsed_count >= CONTENT_PACKAGE_MANIFEST_LIMIT) {
            return CONTENT_PACKAGE_ERR_MANIFEST;
        }
        if (!content_package_parse_item(value, &parsed[parsed_count])) {
            return CONTENT_PACKAGE_ERR_MANIFEST;
        }
        ++parsed_count;
    }
    if (content_library_apply_manifest(parsed, parsed_count) != ESP_OK) {
        return CONTENT_PACKAGE_ERR_STORAGE;
    }
    for (size_t index = 0; index < parsed_count; ++index) {
        if (parsed[index].operation != CONTENT_LIBRARY_OPERATION_UPSERT ||
            parsed[index].entry.state == CONTENT_LIBRARY_STATE_READY) {
            continue;
        }
        content_library_entry_t local_entry = {};
        const bool local_entry_exists =
            content_library_get(
                parsed[index].entry.package_id,
                &local_entry
            ) == ESP_OK;
        if (local_entry_exists &&
            local_entry.state == CONTENT_LIBRARY_STATE_READY &&
            local_entry.package_version == parsed[index].entry.package_version &&
            strcmp(local_entry.sha256, parsed[index].entry.sha256) == 0) {
            continue;
        }
        char path[256] = {0};
        const int written = snprintf(
            path,
            sizeof(path),
            "/packages/%s/download",
            parsed[index].entry.package_id
        );
        if (written <= 0 || (size_t)written >= sizeof(path)) {
            return CONTENT_PACKAGE_ERR_MANIFEST;
        }
        JsonDocument download_document;
        if (content_package_fetch_document(
                path,
                CONTENT_PACKAGE_DOWNLOAD_RESPONSE_SIZE,
                &download_document
            ) != CONTENT_PACKAGE_OK) {
            return CONTENT_PACKAGE_ERR_DOWNLOAD;
        }
        JsonVariantConst download = download_document["data"]["download"];
        const char *download_url = download["download_url"] | "";
        const char *download_sha256 = download["sha256"] | "";
        const int64_t download_size = download["size_bytes"] | int64_t{0};
        if (download_url[0] == '\0' ||
            strncmp(download_url, "https://", 8) != 0 ||
            strlen(download_sha256) != 64 ||
            download_size <= 0 ||
            download_size > (int64_t)CONTENT_DOWNLOADER_MAX_PACKAGE_BYTES) {
            return CONTENT_PACKAGE_ERR_DOWNLOAD;
        }
        content_download_request_t request = {};
        memcpy(
            request.package_id,
            parsed[index].entry.package_id,
            sizeof(request.package_id)
        );
        snprintf(
            request.download_url,
            sizeof(request.download_url),
            "%s",
            download_url
        );
        snprintf(
            request.sha256,
            sizeof(request.sha256),
            "%s",
            download_sha256
        );
        request.size_bytes = (uint64_t)download_size;
        if (content_downloader_enqueue(&request) == ESP_OK) {
            ++applied;
        } else if (all_downloads_queued_out != NULL) {
            *all_downloads_queued_out = false;
        }
    }
    size_t ready = content_library_count();
    size_t pending = applied;
    content_package_runtime_mark_manifest_succeeded(
        &content_package_runtime,
        ready,
        pending
    );
    return CONTENT_PACKAGE_OK;
}

static void content_package_process_withdrawals(
    JsonDocument &document
) {
    JsonVariantConst data = document["data"];
    JsonArrayConst items = data["withdrawn_package_ids"].as<JsonArrayConst>();
    for (JsonVariantConst value : items) {
        const char *package_id = value | "";
        if (package_id[0] == '\0') {
            continue;
        }
        (void)content_downloader_delete(package_id);
        (void)content_library_remove(package_id);
    }
}

static void content_package_worker(void *argument) {
    (void)argument;
    while (!content_package_stop) {
        bool should_sync = false;
        xSemaphoreTake(content_package_lock, portMAX_DELAY);
        if (content_package_runtime.command == CONTENT_PACKAGE_COMMAND_SYNC ||
            content_package_runtime.sync_requested) {
            should_sync = !content_package_runtime.paused;
            content_package_runtime.sync_requested = false;
        }
        xSemaphoreGive(content_package_lock);
        if (!should_sync) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        xSemaphoreTake(content_package_lock, portMAX_DELAY);
        content_package_runtime_mark_manifest_started(&content_package_runtime);
        xSemaphoreGive(content_package_lock);

        JsonDocument document;
        const content_package_error_t fetch_error =
            content_package_fetch_manifest(&document);
        if (fetch_error == CONTENT_PACKAGE_OK) {
            JsonVariantConst data = document["data"];
            JsonArrayConst packages = data["packages"].as<JsonArrayConst>();
            if (packages.isNull()) {
                xSemaphoreTake(content_package_lock, portMAX_DELAY);
                content_package_runtime_mark_failed(
                    &content_package_runtime,
                    CONTENT_PACKAGE_ERR_MANIFEST
                );
                xSemaphoreGive(content_package_lock);
            } else {
                bool all_downloads_queued = true;
                const content_package_error_t apply_error =
                    content_package_apply_manifest(
                        document,
                        &all_downloads_queued
                    );
                if (apply_error == CONTENT_PACKAGE_OK) {
                    content_package_process_withdrawals(document);
                    const int64_t revision =
                        data["catalog_revision"] | content_package_catalog_revision;
                    if (revision < content_package_catalog_revision) {
                        xSemaphoreTake(content_package_lock, portMAX_DELAY);
                        content_package_runtime_mark_manifest_succeeded(
                            &content_package_runtime,
                            content_library_count(),
                            0
                        );
                        xSemaphoreGive(content_package_lock);
                    } else if (revision == content_package_catalog_revision) {
                        if (!all_downloads_queued) {
                            xSemaphoreTake(content_package_lock, portMAX_DELAY);
                            content_package_runtime_mark_failed(
                                &content_package_runtime,
                                CONTENT_PACKAGE_ERR_DOWNLOAD
                            );
                            xSemaphoreGive(content_package_lock);
                        }
                    } else if (all_downloads_queued) {
                        if (content_package_save_revision(revision) == ESP_OK) {
                            content_package_catalog_revision = revision;
                            xSemaphoreTake(content_package_lock, portMAX_DELAY);
                            content_package_runtime_mark_catalog_revision(
                                &content_package_runtime,
                                revision
                            );
                            xSemaphoreGive(content_package_lock);
                        }
                    } else {
                        xSemaphoreTake(content_package_lock, portMAX_DELAY);
                        content_package_runtime_mark_failed(
                            &content_package_runtime,
                            CONTENT_PACKAGE_ERR_DOWNLOAD
                        );
                        xSemaphoreGive(content_package_lock);
                    }
                } else {
                    xSemaphoreTake(content_package_lock, portMAX_DELAY);
                    content_package_runtime_mark_failed(
                        &content_package_runtime,
                        apply_error
                    );
                    xSemaphoreGive(content_package_lock);
                }
            }
        } else {
            xSemaphoreTake(content_package_lock, portMAX_DELAY);
            content_package_runtime_mark_failed(
                &content_package_runtime,
                fetch_error
            );
            xSemaphoreGive(content_package_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(
            CONFIG_CONTENT_PACKAGE_SYNC_INTERVAL_SECONDS * 1000
        ));
    }
    content_package_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t content_package_manager_init(void) {
    if (content_package_manager_ready) {
        return ESP_OK;
    }
    if (!content_library_is_ready() || !content_downloader_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    content_package_lock = xSemaphoreCreateMutex();
    if (content_package_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    content_package_runtime_init(&content_package_runtime);
    content_package_catalog_revision = content_package_load_revision();
    content_package_runtime_mark_catalog_revision(
        &content_package_runtime,
        content_package_catalog_revision
    );
    content_package_stop = false;
    if (xTaskCreate(
            content_package_worker,
            "sprout_packages",
            6144,
            NULL,
            2,
            &content_package_task_handle
        ) != pdPASS) {
        vSemaphoreDelete(content_package_lock);
        content_package_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    content_package_manager_ready = true;
    content_package_runtime_request_sync(&content_package_runtime);
    return ESP_OK;
}

bool content_package_manager_is_ready(void) {
    return content_package_manager_ready;
}

esp_err_t content_package_manager_start(void) {
    if (!content_package_manager_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(content_package_lock, portMAX_DELAY);
    content_package_runtime_request_sync(&content_package_runtime);
    xSemaphoreGive(content_package_lock);
    return ESP_OK;
}

esp_err_t content_package_manager_pause(void) {
    if (!content_package_manager_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(content_package_lock, portMAX_DELAY);
    content_package_runtime_request_pause(&content_package_runtime);
    xSemaphoreGive(content_package_lock);
    return ESP_OK;
}

esp_err_t content_package_manager_resume(void) {
    if (!content_package_manager_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(content_package_lock, portMAX_DELAY);
    content_package_runtime_request_resume(&content_package_runtime);
    content_package_runtime_request_sync(&content_package_runtime);
    xSemaphoreGive(content_package_lock);
    return ESP_OK;
}

esp_err_t content_package_manager_sync_now(void) {
    if (!content_package_manager_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(content_package_lock, portMAX_DELAY);
    content_package_runtime_request_sync(&content_package_runtime);
    xSemaphoreGive(content_package_lock);
    return ESP_OK;
}

esp_err_t content_package_manager_get_snapshot(
    content_package_snapshot_t *snapshot_out
) {
    if (!content_package_manager_ready || snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(content_package_lock, portMAX_DELAY);
    *snapshot_out = content_package_runtime_snapshot(&content_package_runtime);
    snapshot_out->indexed_packages = content_library_count();
    snapshot_out->catalog_revision = content_package_catalog_revision;
    xSemaphoreGive(content_package_lock);
    return ESP_OK;
}

const char *content_package_state_name(content_package_state_t state) {
    switch (state) {
        case CONTENT_PACKAGE_STATE_IDLE:
            return "idle";
        case CONTENT_PACKAGE_STATE_SYNCING_MANIFEST:
            return "syncing_manifest";
        case CONTENT_PACKAGE_STATE_DOWNLOADING:
            return "downloading";
        case CONTENT_PACKAGE_STATE_PAUSED:
            return "paused";
        case CONTENT_PACKAGE_STATE_FAILED:
            return "failed";
        case CONTENT_PACKAGE_STATE_COMPLETE:
            return "complete";
        default:
            return "unknown";
    }
}

const char *content_package_error_name(content_package_error_t error) {
    switch (error) {
        case CONTENT_PACKAGE_OK:
            return "ok";
        case CONTENT_PACKAGE_ERR_NOT_INITIALIZED:
            return "not_initialized";
        case CONTENT_PACKAGE_ERR_INVALID_ARGUMENT:
            return "invalid_argument";
        case CONTENT_PACKAGE_ERR_OFFLINE:
            return "offline";
        case CONTENT_PACKAGE_ERR_UNAUTHENTICATED:
            return "unauthenticated";
        case CONTENT_PACKAGE_ERR_MANIFEST:
            return "manifest";
        case CONTENT_PACKAGE_ERR_STORAGE:
            return "storage";
        case CONTENT_PACKAGE_ERR_DOWNLOAD:
            return "download";
        case CONTENT_PACKAGE_ERR_PAUSED:
            return "paused";
        case CONTENT_PACKAGE_ERR_NOT_FOUND:
            return "not_found";
        default:
            return "unknown";
    }
}

const module_descriptor_t *content_package_manager_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "content_package_manager",
        .version = "1.0.0",
        .initialize = content_package_manager_init,
        .shutdown = NULL,
    };
    return &descriptor;
}
