#include "ArduinoJson.h"

#include "ota_manager.h"

extern "C" {

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cloud_auth.h"
#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if CONFIG_FEATURE_LED_INDICATOR
#include "led_indicator.h"
#endif
#include "network_manager.h"
#include "ota_download.h"
#include "ota_manager_event_sequence.h"
#include "ota_manifest.h"
#include "ota_rollback.h"
#include "ota_validate.h"
#include "time_sync.h"
#include "version_info.h"

#define OTA_MANAGER_RESPONSE_SIZE 8192
#define OTA_MANAGER_URL_SIZE 320
#define OTA_MANAGER_SESSION_TOKEN_SIZE DEVICE_BINDING_SESSION_TOKEN_SIZE
#define OTA_MANAGER_PENDING_RELEASE_KEY "ota_pending_release"
#define OTA_MANAGER_APPLIED_RELEASE_KEY "ota_applied_release"
#define OTA_MANAGER_PENDING_SUCCESS_KEY "ota_pending_success"

static const char *const TAG = "ota_manager";

typedef struct {
    char body[OTA_MANAGER_RESPONSE_SIZE];
    size_t body_length;
    bool overflowed;
} ota_manager_response_t;

typedef struct {
    char release_id[OTA_MANAGER_RELEASE_ID_SIZE];
    char command_id[OTA_MANAGER_COMMAND_ID_SIZE];
} ota_manager_work_item_t;

typedef struct {
    char id[OTA_MANAGER_COMMAND_ID_SIZE];
    char type[32];
    char release_id[OTA_MANAGER_RELEASE_ID_SIZE];
} ota_manager_runtime_command_t;

static bool ota_manager_ready;
static TaskHandle_t ota_manager_task;
static SemaphoreHandle_t ota_manager_mutex;
static QueueHandle_t ota_manager_queue;
static ota_manager_snapshot_t ota_manager_snapshot;
static char ota_manager_last_release_id[OTA_MANAGER_RELEASE_ID_SIZE];
static char ota_manager_last_command_id[OTA_MANAGER_COMMAND_ID_SIZE];
static ota_manager_state_t ota_manager_state;
static uint32_t ota_manager_last_progress_bucket;
static int64_t ota_manager_next_poll_ms;
static const char *ota_manager_event_log[16];
static size_t ota_manager_event_log_count;

static esp_err_t ota_manager_platform_request(
    const char *method,
    const char *path,
    const char *request_body,
    const char *session_token,
    JsonDocument *response_document,
    int *status_code_out
);

static esp_err_t ota_manager_report_event(
    const char *event_name,
    const char *release_id,
    const char *error_code,
    const char *firmware_version,
    const char *artifact_sha256,
    double progress_percent
);

static esp_err_t ota_manager_poll_runtime_firmware_update(
    const char *session_token
);

static esp_err_t ota_manager_load_pending_release(
    char *output,
    size_t output_size
);

static void ota_manager_report_previous_rollback(void);

static void ota_manager_report_pending_success(void);

static void ota_manager_format_utc_timestamp(
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size < 21) {
        return;
    }
    const int64_t epoch = time_sync_get_last_synced_epoch();
    const time_t timestamp = epoch > 0 ? (time_t)epoch : (time_t)0;
    struct tm utc_time = {};
    if (gmtime_r(&timestamp, &utc_time) == NULL) {
        snprintf(output, output_size, "1970-01-01T00:00:00Z");
        return;
    }
    (void)strftime(output, output_size, "%Y-%m-%dT%H:%M:%SZ", &utc_time);
}

static void ota_manager_set_error(const char *code) {
    if (xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    snprintf(
        ota_manager_snapshot.last_error,
        sizeof(ota_manager_snapshot.last_error),
        "%s",
        code != NULL ? code : "unknown"
    );
    xSemaphoreGive(ota_manager_mutex);
}

// The platform event identifier must match the shared lower_snake_case
// contract, so the firmware derives a deterministic id from the release,
// event name, and progress rather than embedding the raw release id.
static void ota_manager_format_event_id(
    const char *release_id,
    const char *event_name,
    double progress_percent,
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) {
        return;
    }
    uint64_t hash = 1469598103934665603ULL;
    const char *const parts[3] = {
        release_id,
        event_name,
        NULL,
    };
    char progress_text[32] = {0};
    snprintf(
        progress_text,
        sizeof(progress_text),
        "%.0f",
        progress_percent < 0.0 ? 0.0 : progress_percent
    );
    for (size_t part = 0; part < 2; ++part) {
        const char *value = parts[part];
        if (value == NULL) {
            continue;
        }
        while (*value != '\0') {
            hash ^= (uint64_t)(unsigned char)*value++;
            hash *= 1099511628211ULL;
        }
    }
    const char *progress_cursor = progress_text;
    while (*progress_cursor != '\0') {
        hash ^= (uint64_t)(unsigned char)*progress_cursor++;
        hash *= 1099511628211ULL;
    }
    snprintf(output, output_size, "ota_evt_%016llx", (unsigned long long)hash);
}

static void ota_manager_set_release(const char *release_id) {
    if (xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    snprintf(
        ota_manager_snapshot.release_id,
        sizeof(ota_manager_snapshot.release_id),
        "%s",
        release_id != NULL ? release_id : ""
    );
    xSemaphoreGive(ota_manager_mutex);
}

static void ota_manager_apply_state_locked(void) {
    ota_manager_snapshot.stage = ota_manager_state.stage;
    ota_manager_snapshot.last_event = ota_manager_state.last_event;
    ota_manager_snapshot.sequence = ota_manager_state.sequence;
    ota_manager_snapshot.received_bytes = ota_manager_state.received_bytes;
    ota_manager_snapshot.total_bytes = ota_manager_state.total_bytes;
}

static void ota_manager_transition(
    ota_manager_stage_t stage,
    ota_manager_event_t event
) {
    if (xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    const bool changed = ota_manager_state_transition(
        &ota_manager_state,
        stage,
        event
    );
    if (!changed) {
        ESP_LOGW(
            TAG,
            "rejected state transition %s -> %s",
            ota_manager_stage_name(ota_manager_state.stage),
            ota_manager_stage_name(stage)
        );
    }
    ota_manager_apply_state_locked();
    xSemaphoreGive(ota_manager_mutex);
}

static void ota_manager_progress_callback(
    uint64_t received_bytes,
    uint64_t total_bytes,
    void *context
) {
    (void)context;
    if (xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    ota_manager_state_set_progress(
        &ota_manager_state,
        received_bytes,
        total_bytes
    );
    ota_manager_apply_state_locked();
    xSemaphoreGive(ota_manager_mutex);

    if (total_bytes == 0) {
        return;
    }
    const uint32_t bucket = (uint32_t)((received_bytes * 10ULL) / total_bytes);
    if (bucket == ota_manager_last_progress_bucket) {
        return;
    }
    ota_manager_last_progress_bucket = bucket;
    char release_id[OTA_MANAGER_RELEASE_ID_SIZE] = {0};
    if (xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) == pdTRUE) {
        snprintf(
            release_id,
            sizeof(release_id),
            "%s",
            ota_manager_snapshot.release_id
        );
        xSemaphoreGive(ota_manager_mutex);
    }
    if (release_id[0] == '\0') {
        (void)ota_manager_load_pending_release(
            release_id,
            sizeof(release_id)
        );
    }
    if (release_id[0] != '\0') {
        const uint32_t progress_percent =
            bucket >= 10 ? 100U : bucket * 10U;
        (void)ota_manager_report_event(
            "downloading",
            release_id,
            NULL,
            NULL,
            NULL,
            (double)progress_percent
        );
    }
}

static bool ota_manager_cancel_callback(void *context) {
    (void)context;
    return !ota_manager_ready;
}

static esp_err_t ota_manager_response_handler(
    esp_http_client_event_t *event
) {
    ota_manager_response_t *response =
        (ota_manager_response_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL ||
        event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t remaining =
        sizeof(response->body) - 1 - response->body_length;
    const size_t copy_size = (size_t)event->data_len < remaining
                                 ? (size_t)event->data_len
                                 : remaining;
    if (copy_size == 0) {
        response->overflowed = true;
        return ESP_OK;
    }
    if (copy_size < (size_t)event->data_len) {
        response->overflowed = true;
    }
    memcpy(
        response->body + response->body_length,
        event->data,
        copy_size
    );
    response->body_length += copy_size;
    response->body[response->body_length] = '\0';
    return ESP_OK;
}

static esp_err_t ota_manager_platform_request(
    const char *method,
    const char *path,
    const char *request_body,
    const char *session_token,
    JsonDocument *response_document,
    int *status_code_out
) {
    char base_url[OTA_MANAGER_URL_SIZE] = {0};
    esp_err_t result = device_binding_client_get_platform_base_url(
        base_url,
        sizeof(base_url)
    );
    if (result != ESP_OK) {
        return result;
    }
    if (strncmp(base_url, "https://", 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[OTA_MANAGER_URL_SIZE] = {0};
    const int written = snprintf(url, sizeof(url), "%s%s", base_url, path);
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    ota_manager_response_t response = {};
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 30000;
    config.event_handler = ota_manager_response_handler;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    if (strcmp(method, "POST") == 0) {
        config.method = HTTP_METHOD_POST;
    }
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (session_token != NULL && session_token[0] != '\0') {
        char authorization[OTA_MANAGER_SESSION_TOKEN_SIZE + 16] = {0};
        snprintf(
            authorization,
            sizeof(authorization),
            "Bearer %s",
            session_token
        );
        esp_http_client_set_header(client, "Authorization", authorization);
        memset(authorization, 0, sizeof(authorization));
    }
    if (request_body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(
            client,
            request_body,
            (int)strlen(request_body)
        );
    }
    result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status_code_out != NULL) {
        *status_code_out = status_code;
    }
    if (result != ESP_OK) {
        return result;
    }
    if (status_code < 200 || status_code >= 300) {
        return status_code == 401 || status_code == 403
                   ? ESP_ERR_INVALID_STATE
                   : ESP_ERR_INVALID_RESPONSE;
    }
    if (response_document == NULL) {
        return ESP_OK;
    }
    if (response.overflowed) {
        return ESP_ERR_INVALID_SIZE;
    }
    JsonDocument envelope;
    if (deserializeJson(envelope, response.body) != DeserializationError::Ok) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    JsonVariant data = envelope["data"];
    if (!data.is<JsonObjectConst>()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    response_document->set(data);
    return ESP_OK;
}

static esp_err_t ota_manager_get_session_token(
    char *token,
    size_t token_size
) {
    const esp_err_t auth_result = cloud_auth_ensure_authenticated();
    if (auth_result != ESP_OK) {
        return auth_result;
    }
    return device_binding_client_copy_session_token(token, token_size);
}

static esp_err_t ota_manager_fetch_manifest(
    const char *release_id,
    ota_manifest_t *manifest_out
) {
    if (release_id == NULL || manifest_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(manifest_out, 0, sizeof(*manifest_out));
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }
    char session_token[OTA_MANAGER_SESSION_TOKEN_SIZE] = {0};
    result = ota_manager_get_session_token(
        session_token,
        sizeof(session_token)
    );
    if (result != ESP_OK) {
        return result;
    }
    char path[OTA_MANAGER_URL_SIZE] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/ota",
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_SIZE;
    }
    JsonDocument response;
    result = ota_manager_platform_request(
        "GET",
        path,
        NULL,
        session_token,
        &response,
        NULL
    );
    memset(session_token, 0, sizeof(session_token));
    if (result != ESP_OK) {
        return result;
    }
    JsonObject release_value = response["release"].as<JsonObject>();
    if (release_value.isNull()) {
        return ESP_ERR_NOT_FOUND;
    }
    const char *release_from_platform = release_value["release_id"] | "";
    if (release_id[0] != '\0' &&
        strcmp(release_id, release_from_platform) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const char *schema_version = release_value["schema_version"] | "";
    if (schema_version[0] != '\0' &&
        strncmp(schema_version, "1", 1) != 0) {
        return ESP_ERR_INVALID_VERSION;
    }
    const char *version = release_value["firmware_version"] | "";
    if (version[0] == '\0') {
        version = release_value["version"] | "";
    }
    char fallback_published_at[OTA_MANIFEST_TIME_SIZE] = {0};
    const char *published_at = release_value["published_at"] | "";
    if (published_at[0] == '\0') {
        ota_manager_format_utc_timestamp(
            fallback_published_at,
            sizeof(fallback_published_at)
        );
        published_at = fallback_published_at;
    }
    // The platform response also carries operator-only fields. Adapt only the
    // security-relevant projection here so unknown fields cannot widen the
    // strict on-device manifest contract.
    const ota_platform_release_t platform_release = {
        .release_id = release_from_platform,
        .version = version,
        .hardware_revision = release_value["hardware_revision"] | "",
        .channel = release_value["channel"] | "",
        .artifact_url = release_value["artifact_url"] | "",
        .sha256 = release_value["sha256"] | "",
        .size_bytes = release_value["size_bytes"] | (uint64_t)0,
        .signature_key_id = release_value["signature_key_id"] | "",
        .signature = release_value["signature"] | "",
        .rollback_allowed = release_value["rollback_allowed"] | false,
        .published_at = published_at,
        .min_source_version = release_value["min_source_version"] | "",
    };
    const ota_manifest_error_t parse_result =
        ota_manifest_from_platform_release(
            &platform_release,
            fallback_published_at,
            manifest_out
        );
    if (parse_result != OTA_MANIFEST_OK) {
        ESP_LOGW(TAG, "manifest rejected: %s",
                 ota_manifest_error_name(parse_result));
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t ota_manager_report_event(
    const char *event_name,
    const char *release_id,
    const char *error_code,
    const char *firmware_version,
    const char *artifact_sha256,
    double progress_percent
) {
    if (event_name == NULL || release_id == NULL ||
        event_name[0] == '\0' || release_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t event_index = ota_manager_event_log_count;
    const ota_event_sequence_error_t sequence_result =
        ota_event_sequence_append(
            ota_manager_event_log,
            event_index,
            sizeof(ota_manager_event_log) /
                sizeof(ota_manager_event_log[0]),
            event_name
        );
    if (sequence_result != OTA_EVENT_SEQUENCE_OK) {
        ESP_LOGW(
            TAG,
            "OTA event rejected by sequence guard: %s",
            event_name
        );
        return ESP_ERR_INVALID_STATE;
    }
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }
    char session_token[OTA_MANAGER_SESSION_TOKEN_SIZE] = {0};
    result = ota_manager_get_session_token(
        session_token,
        sizeof(session_token)
    );
    if (result != ESP_OK) {
        return result;
    }

    JsonDocument payload;
    payload["schema_version"] = "1.0.0";
    char event_id[128] = {0};
    const char *firmware_for_event = firmware_version;
    if (firmware_for_event == NULL || firmware_for_event[0] == '\0') {
        firmware_for_event = version_info_get().firmware_version;
    }
    ota_manager_format_event_id(
        release_id,
        event_name,
        progress_percent,
        event_id,
        sizeof(event_id)
    );
    if (event_id[0] == '\0') {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_SIZE;
    }
    payload["event_id"] = event_id;
    payload["release_id"] = release_id;
    payload["event_type"] = event_name;
    payload["progress_percent"] = progress_percent;
    payload["firmware_version"] = firmware_for_event;
    if (error_code != NULL && error_code[0] != '\0') {
        payload["error_code"] = error_code;
    }
    if (artifact_sha256 != NULL && artifact_sha256[0] != '\0') {
        payload["artifact_sha256"] = artifact_sha256;
    }
    char occurred_at[21] = {0};
    ota_manager_format_utc_timestamp(occurred_at, sizeof(occurred_at));
    payload["occurred_at"] = occurred_at;
    char request_body[1024] = {0};
    const size_t request_size = serializeJson(payload, request_body);
    if (request_size == 0 || request_size >= sizeof(request_body)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_SIZE;
    }
    char path[OTA_MANAGER_URL_SIZE] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/ota/events",
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_SIZE;
    }
    result = ota_manager_platform_request(
        "POST",
        path,
        request_body,
        session_token,
        NULL,
        NULL
    );
    memset(request_body, 0, sizeof(request_body));
    memset(event_id, 0, sizeof(event_id));
    memset(session_token, 0, sizeof(session_token));
    if (result == ESP_OK) {
        ota_manager_event_log_count = event_index + 1;
    } else {
        ota_manager_event_log[event_index] = NULL;
    }
    return result;
}

static esp_err_t ota_manager_poll_update(void) {
    // Retry a rollback that could not be reported before the network was
    // ready, then a success that could not be reported, so the platform never
    // misses a terminal event.
    ota_manager_report_previous_rollback();
    ota_manager_report_pending_success();
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }
    char session_token[OTA_MANAGER_SESSION_TOKEN_SIZE] = {0};
    result = ota_manager_get_session_token(
        session_token,
        sizeof(session_token)
    );
    if (result != ESP_OK) {
        return result;
    }
    char path[OTA_MANAGER_URL_SIZE] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/ota",
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_SIZE;
    }
    JsonDocument response;
    result = ota_manager_platform_request(
        "GET",
        path,
        NULL,
        session_token,
        &response,
        NULL
    );
    if (ota_rollback_is_pending_verify()) {
        (void)ota_manager_mark_running_image_healthy();
    }
    const esp_err_t command_result =
        ota_manager_poll_runtime_firmware_update(session_token);
    memset(session_token, 0, sizeof(session_token));
    if (command_result == ESP_OK) {
        return command_result;
    }
    if (result == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (result != ESP_OK) {
        return result;
    }
    const char *release_id = response["release"]["release_id"] | "";
    if (release_id[0] == '\0') {
        return ESP_OK;
    }
    if (strcmp(release_id, ota_manager_last_release_id) == 0) {
        return ESP_OK;
    }
    return ota_manager_handle_firmware_update_command(NULL, release_id);
}

static esp_err_t ota_manager_record_pending_release(
    const char *release_id
) {
    if (release_id == NULL || release_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return config_store_set_string(
        OTA_MANAGER_PENDING_RELEASE_KEY,
        release_id
    );
}

static esp_err_t ota_manager_clear_pending_release(void) {
    return config_store_erase_key(OTA_MANAGER_PENDING_RELEASE_KEY);
}

// After an OTA reboot the in-memory snapshot is empty, so the applied release
// id must be restored from the persisted pending record.
static esp_err_t ota_manager_load_pending_release(
    char *output,
    size_t output_size
) {
    if (output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return config_store_get_string(
        OTA_MANAGER_PENDING_RELEASE_KEY,
        output,
        output_size
    );
}

static void ota_manager_load_applied_release(void) {
    char applied_release[OTA_MANAGER_RELEASE_ID_SIZE] = {0};
    if (config_store_get_string(
            OTA_MANAGER_APPLIED_RELEASE_KEY,
            applied_release,
            sizeof(applied_release)
        ) != ESP_OK) {
        return;
    }
    snprintf(
        ota_manager_last_release_id,
        sizeof(ota_manager_last_release_id),
        "%s",
        applied_release
    );
    memset(applied_release, 0, sizeof(applied_release));
}

static esp_err_t ota_manager_record_applied_release(
    const char *release_id
) {
    if (release_id == NULL || release_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return config_store_set_string(
        OTA_MANAGER_APPLIED_RELEASE_KEY,
        release_id
    );
}

static void ota_manager_report_previous_rollback(void) {
    char pending_release[OTA_MANAGER_RELEASE_ID_SIZE] = {0};
    const esp_err_t read_result = config_store_get_string(
        OTA_MANAGER_PENDING_RELEASE_KEY,
        pending_release,
        sizeof(pending_release)
    );
    if (read_result == CONFIG_STORE_ERR_NOT_FOUND) {
        return;
    }
    if (read_result != ESP_OK) {
        return;
    }
    const esp_partition_t *invalid_partition =
        esp_ota_get_last_invalid_partition();
    if (invalid_partition == NULL || ota_rollback_is_pending_verify()) {
        // Either this is a normal boot or the image is still awaiting its
        // health check. Keep the pending record so a later report can use it.
        memset(pending_release, 0, sizeof(pending_release));
        return;
    }
    ota_manager_transition(
        OTA_MANAGER_STAGE_ROLLED_BACK,
        OTA_MANAGER_EVENT_ROLLED_BACK
    );
    const esp_err_t report_result = ota_manager_report_event(
        "rolled_back",
        pending_release,
        "boot_validation_failed",
        NULL,
        NULL,
        100.0
    );
    if (report_result == ESP_OK) {
        // Only forget the pending record once the platform has acknowledged
        // the rollback; otherwise the next network-ready poll retries it.
        (void)ota_manager_clear_pending_release();
    }
    memset(pending_release, 0, sizeof(pending_release));
}

// A successful image can boot before the network is ready. Persist the
// validated release and retry its terminal `succeeded` event on later polls
// until the platform acknowledges it.
static void ota_manager_report_pending_success(void) {
    char pending_success[OTA_MANAGER_RELEASE_ID_SIZE] = {0};
    if (config_store_get_string(
            OTA_MANAGER_PENDING_SUCCESS_KEY,
            pending_success,
            sizeof(pending_success)
        ) != ESP_OK ||
        pending_success[0] == '\0') {
        memset(pending_success, 0, sizeof(pending_success));
        return;
    }
    const esp_err_t report_result = ota_manager_report_event(
        "succeeded",
        pending_success,
        NULL,
        version_info_get().firmware_version,
        NULL,
        100.0
    );
    if (report_result == ESP_OK) {
        (void)config_store_erase_key(OTA_MANAGER_PENDING_SUCCESS_KEY);
    }
    memset(pending_success, 0, sizeof(pending_success));
}

static esp_err_t ota_manager_poll_runtime_firmware_update(
    const char *session_token
) {
    if (session_token == NULL || session_token[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }
    char path[OTA_MANAGER_URL_SIZE] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/runtime/commands",
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    JsonDocument response;
    result = ota_manager_platform_request(
        "GET",
        path,
        NULL,
        session_token,
        &response,
        NULL
    );
    if (result != ESP_OK) {
        return result;
    }
    JsonArray commands = response["commands"].as<JsonArray>();
    for (JsonVariant command_value : commands) {
        ota_manager_runtime_command_t command = {};
        const char *command_id = command_value["command_id"] | "";
        const char *command_type = command_value["command_type"] | "";
        const char *command_status = command_value["status"] | "";
        const char *release_id = command_value["release_id"] | "";
        if (strcmp(command_type, "firmware_update") != 0 ||
            (strcmp(command_status, "pending") != 0 &&
             strcmp(command_status, "delivered") != 0) ||
            command_id[0] == '\0' || release_id[0] == '\0' ||
            strlen(command_id) >= sizeof(command.id) ||
            strlen(command_type) >= sizeof(command.type) ||
            strlen(release_id) >= sizeof(command.release_id)) {
            continue;
        }
        snprintf(command.id, sizeof(command.id), "%s", command_id);
        snprintf(command.type, sizeof(command.type), "%s", command_type);
        snprintf(
            command.release_id,
            sizeof(command.release_id),
            "%s",
            release_id
        );
        return ota_manager_handle_firmware_update_command(
            command.id,
            command.release_id
        );
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ota_manager_mark_running_image_healthy(void) {
    if (!ota_manager_ready || !ota_rollback_is_pending_verify()) {
        return ESP_OK;
    }
    char release_id[OTA_MANAGER_RELEASE_ID_SIZE] = {0};
    if (xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) == pdTRUE) {
        snprintf(
            release_id,
            sizeof(release_id),
            "%s",
            ota_manager_snapshot.release_id
        );
        xSemaphoreGive(ota_manager_mutex);
    }
    ota_manager_event_log_count = 0;
    memset(
        ota_manager_event_log,
        0,
        sizeof(ota_manager_event_log)
    );
    if (ota_manager_state.stage != OTA_MANAGER_STAGE_PENDING_VERIFY) {
        ota_manager_state.stage = OTA_MANAGER_STAGE_PENDING_VERIFY;
        ota_manager_state.last_event = OTA_MANAGER_EVENT_INSTALLED;
    }
    const esp_err_t result = ota_rollback_mark_valid();
    if (result != ESP_OK) {
        return result;
    }
    if (release_id[0] != '\0') {
        (void)ota_manager_record_applied_release(release_id);
    }
    ota_manager_transition(
        OTA_MANAGER_STAGE_VALID,
        OTA_MANAGER_EVENT_INSTALLED
    );
    if (xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) == pdTRUE) {
        ota_manager_snapshot.pending_reboot = false;
        ota_manager_apply_state_locked();
        xSemaphoreGive(ota_manager_mutex);
    }
    const esp_err_t report_result = ota_manager_report_event(
        "succeeded",
        release_id,
        NULL,
        version_info_get().firmware_version,
        NULL,
        100.0
    );
    if (report_result != ESP_OK && release_id[0] != '\0') {
        // Preserve the terminal event so a later healthy poll retries it.
        (void)config_store_set_string(
            OTA_MANAGER_PENDING_SUCCESS_KEY,
            release_id
        );
    }
    (void)ota_manager_clear_pending_release();
    return ESP_OK;
}

static esp_err_t ota_manager_install_manifest(
    const ota_manifest_t *manifest,
    const char *command_id
) {
    if (manifest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_partition_t *target =
        esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    ota_manager_transition(
        OTA_MANAGER_STAGE_DOWNLOADING,
        OTA_MANAGER_EVENT_STARTED
    );
    ota_download_request_t request = {
        .url = manifest->artifact_url,
        .expected_sha256 = manifest->sha256,
        .expected_size = manifest->size_bytes,
        .maximum_retries = CONFIG_OTA_DOWNLOAD_MAX_RETRIES,
        .timeout_ms = CONFIG_OTA_DOWNLOAD_TIMEOUT_SECONDS * 1000U,
    };
    esp_err_t result = ota_download_to_partition(
        &request,
        target,
        ota_manager_progress_callback,
        NULL,
        ota_manager_cancel_callback,
        NULL
    );
    if (result != ESP_OK) {
        return result;
    }
    (void)ota_manager_report_event(
        "downloaded",
        manifest->release_id,
        NULL,
        manifest->firmware_version,
        manifest->sha256,
        100.0
    );
    ota_manager_transition(
        OTA_MANAGER_STAGE_VALIDATING,
        OTA_MANAGER_EVENT_PROGRESS
    );
    result = ota_validate_partition(target, manifest);
    if (result != ESP_OK) {
        return result;
    }
    ota_manager_transition(
        OTA_MANAGER_STAGE_INSTALLING,
        OTA_MANAGER_EVENT_VALIDATED
    );
    (void)ota_manager_report_event(
        "installing",
        manifest->release_id,
        NULL,
        manifest->firmware_version,
        manifest->sha256,
        100.0
    );
    result = esp_ota_set_boot_partition(target);
    if (result != ESP_OK) {
        return result;
    }
    const esp_err_t pending_result =
        ota_manager_record_pending_release(manifest->release_id);
    if (pending_result != ESP_OK) {
        return pending_result;
    }
    ota_manager_transition(
        OTA_MANAGER_STAGE_PENDING_VERIFY,
        OTA_MANAGER_EVENT_INSTALLED
    );
    (void)ota_manager_report_event(
        "validated",
        manifest->release_id,
        NULL,
        manifest->firmware_version,
        manifest->sha256,
        100.0
    );
    (void)ota_manager_report_event(
        "pending_verify",
        manifest->release_id,
        NULL,
        manifest->firmware_version,
        manifest->sha256,
        100.0
    );
    if (xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) == pdTRUE) {
        ota_manager_snapshot.pending_reboot = true;
        ota_manager_apply_state_locked();
        xSemaphoreGive(ota_manager_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_manager_execute(
    const char *release_id,
    const char *command_id
) {
    if (release_id == NULL || release_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (ota_rollback_is_pending_verify()) {
        return ESP_OK;
    }
    if (strcmp(release_id, ota_manager_last_release_id) == 0 &&
        command_id != NULL &&
        strcmp(command_id, ota_manager_last_command_id) == 0) {
        return ESP_OK;
    }
    ota_manager_event_log_count = 0;
    memset(
        ota_manager_event_log,
        0,
        sizeof(ota_manager_event_log)
    );
    ota_manager_set_release(release_id);
    ota_manager_transition(
        OTA_MANAGER_STAGE_CHECKING,
        OTA_MANAGER_EVENT_STARTED
    );
    (void)ota_manager_report_event(
        "started",
        release_id,
        NULL,
        NULL,
        NULL,
        0.0
    );
    ota_manifest_t manifest = {};
    esp_err_t result = ota_manager_fetch_manifest(release_id, &manifest);
    if (result != ESP_OK) {
        ota_manager_set_error("manifest_unavailable");
        return result;
    }
    const version_info_t versions = version_info_get();
    const ota_manifest_error_t validation = ota_manifest_validate_device(
        &manifest,
        versions.firmware_version,
        CONFIG_OTA_MANAGER_HARDWARE_REVISION,
        CONFIG_OTA_MANAGER_CHANNEL,
        false
    );
    if (validation != OTA_MANIFEST_OK) {
        ESP_LOGW(TAG, "device validation failed: %s",
                 ota_manifest_error_name(validation));
        ota_manager_set_error(ota_manifest_error_name(validation));
        ota_manager_transition(
            OTA_MANAGER_STAGE_FAILED,
            OTA_MANAGER_EVENT_FAILED
        );
        (void)ota_manager_report_event(
            "failed",
            release_id,
            ota_manifest_error_name(validation),
            manifest.firmware_version,
            manifest.sha256,
            0.0
        );
        return ESP_ERR_INVALID_VERSION;
    }
    result = ota_manager_install_manifest(&manifest, command_id);
    if (result != ESP_OK) {
        ota_manager_set_error("install_failed");
        ota_manager_transition(
            OTA_MANAGER_STAGE_FAILED,
            OTA_MANAGER_EVENT_FAILED
        );
        (void)ota_manager_report_event(
            "failed",
            release_id,
            "install_failed",
            manifest.firmware_version,
            manifest.sha256,
            0.0
        );
        return result;
    }
    if (command_id != NULL) {
        snprintf(
            ota_manager_last_command_id,
            sizeof(ota_manager_last_command_id),
            "%s",
            command_id
        );
    }
    snprintf(
        ota_manager_last_release_id,
        sizeof(ota_manager_last_release_id),
        "%s",
        release_id
    );
    return ESP_OK;
}

static void ota_manager_work_task(void *argument) {
    (void)argument;
    while (ota_manager_ready) {
        ota_manager_work_item_t item = {};
        if (xQueueReceive(
                ota_manager_queue,
                &item,
                pdMS_TO_TICKS(5000)
            ) != pdTRUE) {
            const int64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms >= ota_manager_next_poll_ms &&
                network_manager_get_state() ==
                    NETWORK_MANAGER_STATE_CONNECTED &&
                time_sync_is_synchronized() &&
                device_binding_client_is_bound()) {
                (void)ota_manager_poll_update();
                ota_manager_next_poll_ms = now_ms +
                    (int64_t)CONFIG_OTA_MANAGER_POLL_INTERVAL_SECONDS * 1000;
            }
            continue;
        }
        if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED ||
            !time_sync_is_synchronized() ||
            !device_binding_client_is_bound()) {
            ota_manager_set_error("network_unavailable");
            continue;
        }
#if CONFIG_FEATURE_LED_INDICATOR
        (void)led_indicator_set_state(LED_INDICATOR_STATE_OTA_UPGRADING);
#endif
        const esp_err_t result = ota_manager_execute(
            item.release_id,
            item.command_id[0] != '\0' ? item.command_id : NULL
        );
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "OTA update failed: %s", esp_err_to_name(result));
        }
#if CONFIG_FEATURE_LED_INDICATOR
        (void)led_indicator_set_state(
            result == ESP_OK ? LED_INDICATOR_STATE_IDLE
                             : LED_INDICATOR_STATE_ERROR
        );
#endif
    }
    ota_manager_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ota_manager_init(void) {
    if (ota_manager_ready) {
        return ESP_OK;
    }
    const esp_err_t rollback_result = ota_rollback_init();
    if (rollback_result != ESP_OK) {
        return rollback_result;
    }
    ota_manager_mutex = xSemaphoreCreateMutex();
    ota_manager_queue = xQueueCreate(4, sizeof(ota_manager_work_item_t));
    if (ota_manager_mutex == NULL || ota_manager_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ota_manager_state_init(&ota_manager_state);
    memset(&ota_manager_snapshot, 0, sizeof(ota_manager_snapshot));
    ota_manager_snapshot.ready = true;
    ota_manager_apply_state_locked();
    ota_manager_load_applied_release();
    if (ota_rollback_is_pending_verify()) {
        char pending_release[OTA_MANAGER_RELEASE_ID_SIZE] = {0};
        if (ota_manager_load_pending_release(
                pending_release,
                sizeof(pending_release)
            ) == ESP_OK) {
            snprintf(
                ota_manager_snapshot.release_id,
                sizeof(ota_manager_snapshot.release_id),
                "%s",
                pending_release
            );
        }
        memset(pending_release, 0, sizeof(pending_release));
        ota_manager_state.stage = OTA_MANAGER_STAGE_PENDING_VERIFY;
        ota_manager_state.last_event = OTA_MANAGER_EVENT_INSTALLED;
        ota_manager_snapshot.stage = OTA_MANAGER_STAGE_PENDING_VERIFY;
        ota_manager_snapshot.last_event = OTA_MANAGER_EVENT_INSTALLED;
        ota_manager_snapshot.pending_reboot = true;
    }
    ota_manager_ready = true;
    if (xTaskCreate(
            ota_manager_work_task,
            "sprout_ota",
            CONFIG_OTA_MANAGER_TASK_STACK_SIZE,
            NULL,
            CONFIG_OTA_MANAGER_TASK_PRIORITY,
            &ota_manager_task
        ) != pdPASS) {
        ota_manager_ready = false;
        return ESP_ERR_NO_MEM;
    }
    if (ota_rollback_is_pending_verify()) {
        (void)ota_rollback_reboot_if_unhealthy();
    }
    return ESP_OK;
}

bool ota_manager_is_ready(void) {
    return ota_manager_ready;
}

ota_manager_snapshot_t ota_manager_get_snapshot(void) {
    ota_manager_snapshot_t snapshot = {};
    if (ota_manager_mutex == NULL ||
        xSemaphoreTake(ota_manager_mutex, portMAX_DELAY) != pdTRUE) {
        return snapshot;
    }
    snapshot = ota_manager_snapshot;
    xSemaphoreGive(ota_manager_mutex);
    return snapshot;
}

esp_err_t ota_manager_handle_firmware_update_command(
    const char *command_id,
    const char *release_id
) {
    if (release_id == NULL || release_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_manager_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ota_manager_work_item_t item = {};
    snprintf(item.release_id, sizeof(item.release_id), "%s", release_id);
    if (command_id != NULL) {
        snprintf(item.command_id, sizeof(item.command_id), "%s", command_id);
    }
    return xQueueSend(ota_manager_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

const module_descriptor_t *ota_manager_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "ota_manager",
        .version = "1.0.0",
        .initialize = ota_manager_init,
        .shutdown = NULL,
    };
    return &descriptor;
}

}  // extern "C"
