#include "vision_sync.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "camera_vision.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "time_sync.h"
#include "vision_sync_core.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define VISION_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define VISION_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_VISION_SYNC_INTERVAL_SECONDS
#define CONFIG_VISION_SYNC_INTERVAL_SECONDS 30
#endif
#ifndef CONFIG_VISION_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_VISION_SYNC_RETRY_INTERVAL_SECONDS 15
#endif
#ifndef CONFIG_VISION_SYNC_MAX_TASKS_PER_CYCLE
#define CONFIG_VISION_SYNC_MAX_TASKS_PER_CYCLE 4
#endif

#define VISION_SYNC_URL_SIZE 384
#define VISION_SYNC_RESPONSE_SIZE 16384
#define VISION_SYNC_REQUEST_SIZE 2048

static const char *const TAG = "vision_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;
static vision_sync_provider_t s_provider;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} vision_sync_response_t;

static esp_err_t vision_sync_response_init(vision_sync_response_t *response) {
    response->body = (char *)malloc(VISION_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = VISION_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void vision_sync_response_destroy(vision_sync_response_t *response) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t vision_sync_http_event(esp_http_client_event_t *event) {
    vision_sync_response_t *response =
        (vision_sync_response_t *)event->user_data;
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

static esp_err_t vision_sync_prepare(
    char *session_token,
    size_t session_token_size,
    char *device_id,
    size_t device_id_size,
    char *base_url,
    size_t base_url_size
) {
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED ||
        !time_sync_is_synchronized() ||
        !device_binding_client_is_registered()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device_binding_client_copy_session_token(
            session_token,
            session_token_size
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device_identity_copy(device_id, device_id_size) != ESP_OK) {
        memset(session_token, 0, session_token_size);
        return ESP_ERR_INVALID_STATE;
    }
    if (device_binding_client_get_platform_base_url(
            base_url,
            base_url_size
        ) != ESP_OK ||
        strncmp(base_url, "https://", 8) != 0) {
        memset(session_token, 0, session_token_size);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// Issue one authenticated request and leave the decoded body in `response`.
// The caller owns the token buffers so a 401/403 can clear the shared session.
static esp_err_t vision_sync_request(
    const char *method,
    const char *url,
    const char *session_token,
    const char *request_body,
    vision_sync_response_t *response
) {
#if VISION_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif
    if (vision_sync_response_init(response) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method =
        strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = vision_sync_http_event;
    config.user_data = response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        vision_sync_response_destroy(response);
        return ESP_ERR_NO_MEM;
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
    if (request_body != nullptr) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(
            client,
            request_body,
            (int)strlen(request_body)
        );
    }
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    if (transport_result != ESP_OK) {
        vision_sync_response_destroy(response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        vision_sync_response_destroy(response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 404) {
        vision_sync_response_destroy(response);
        return ESP_ERR_NOT_FOUND;
    }
    if (status_code < 200 || status_code >= 300) {
        vision_sync_response_destroy(response);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Map the four platform task types onto the firmware enum. The firmware-only
// `capture` type is intentionally not accepted from the platform.
static bool vision_sync_task_type_from_name(
    const char *name,
    camera_vision_task_type_t *task_type_out
) {
    if (name == NULL || task_type_out == NULL) {
        return false;
    }
    if (strcmp(name, "object_recognition") == 0) {
        *task_type_out = CAMERA_VISION_TASK_OBJECT_RECOGNITION;
        return true;
    }
    if (strcmp(name, "picture_book_recognition") == 0) {
        *task_type_out = CAMERA_VISION_TASK_PICTURE_BOOK_RECOGNITION;
        return true;
    }
    if (strcmp(name, "photo_qa") == 0) {
        *task_type_out = CAMERA_VISION_TASK_PHOTO_QA;
        return true;
    }
    return false;
}

static esp_err_t vision_sync_fetch_tasks(
    vision_sync_task_t *tasks,
    size_t task_capacity,
    size_t *task_count_out
) {
    if (tasks == NULL || task_count_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *task_count_out = 0;
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[VISION_SYNC_URL_SIZE] = {0};
    if (vision_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char url[VISION_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/vision/tasks/pending",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
    vision_sync_response_t response = {};
    const esp_err_t request_result = vision_sync_request(
        "GET",
        url,
        session_token,
        nullptr,
        &response
    );
    memset(session_token, 0, sizeof(session_token));
    if (request_result != ESP_OK) {
        return request_result;
    }
    if (response.overflowed) {
        vision_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    if (deserializeJson(document, response.body) != DeserializationError::Ok) {
        vision_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    vision_sync_response_destroy(&response);

    JsonArrayConst json_tasks =
        document["data"]["tasks"].as<JsonArrayConst>();
    if (json_tasks.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    for (JsonVariantConst value : json_tasks) {
        if (*task_count_out >= task_capacity) {
            break;
        }
        vision_sync_task_t task = {};
        if (!vision_sync_core_copy_field(
                task.task_id,
                sizeof(task.task_id),
                value["task_id"] | ""
            )) {
            continue;
        }
        if (!vision_sync_task_type_from_name(
                value["task_type"] | "",
                &task.task_type
            )) {
            continue;
        }
        if (!vision_sync_core_status_from_name(
                value["status"] | "",
                &task.status
            )) {
            continue;
        }
        (void)vision_sync_core_copy_field(
            task.prompt,
            sizeof(task.prompt),
            value["prompt"] | ""
        );
        tasks[*task_count_out] = task;
        *task_count_out += 1u;
    }
    return ESP_OK;
}

// POST one lifecycle report. `body` may be null for an empty acknowledge.
static esp_err_t vision_sync_post(
    const char *url,
    const char *session_token,
    const char *body
) {
    vision_sync_response_t response = {};
    const esp_err_t result = vision_sync_request(
        "POST",
        url,
        session_token,
        body,
        &response
    );
    vision_sync_response_destroy(&response);
    return result;
}

static esp_err_t vision_sync_acknowledge(
    const vision_sync_task_t *task,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[VISION_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/vision/tasks/%s/acknowledge",
        base_url,
        device_id,
        task->task_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    return vision_sync_post(url, session_token, nullptr);
}

static esp_err_t vision_sync_report_failure(
    const vision_sync_task_t *task,
    const char *failure_code,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[VISION_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/vision/tasks/%s/status",
        base_url,
        device_id,
        task->task_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument payload;
    payload["status"] = "failed";
    payload["failure_code"] = failure_code;
    char body[VISION_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length = serializeJson(payload, body, sizeof(body));
    if (body_length == 0 || body_length >= sizeof(body)) {
        return ESP_ERR_INVALID_ARG;
    }
    return vision_sync_post(url, session_token, body);
}

static esp_err_t vision_sync_report_result(
    const vision_sync_task_t *task,
    const vision_sync_result_t *result,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[VISION_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/vision/results",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument payload;
    payload["task_id"] = task->task_id;
    payload["frame_width"] = result->frame_width;
    payload["frame_height"] = result->frame_height;
    payload["frame_bytes"] = result->frame_bytes;
    payload["summary"] = result->summary;
    JsonArray labels = payload["labels"].to<JsonArray>();
    for (size_t index = 0; index < result->label_count; ++index) {
        JsonObject label = labels.add<JsonObject>();
        label["label"] = result->labels[index].label;
        label["confidence"] = (int)result->labels[index].confidence;
    }
    char body[VISION_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length = serializeJson(payload, body, sizeof(body));
    if (body_length == 0 || body_length >= sizeof(body)) {
        return ESP_ERR_INVALID_ARG;
    }
    return vision_sync_post(url, session_token, body);
}

static esp_err_t vision_sync_run_task(
    const vision_sync_task_t *task,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    // Mark the task dispatched before inference so a crash mid-task leaves the
    // platform in a state it can expire instead of redispatching forever.
    const esp_err_t ack_result = vision_sync_acknowledge(
        task,
        session_token,
        device_id,
        base_url
    );
    if (ack_result != ESP_OK) {
        return ack_result;
    }
    const camera_vision_error_t accept_result = camera_vision_accept_task(
        task->task_id,
        task->task_type
    );
    if (accept_result != CAMERA_VISION_OK) {
        const char *failure_code =
            accept_result == CAMERA_VISION_ERR_PRIVACY_CONSENT_REQUIRED
                ? "consent_revoked"
                : "invalid_frame";
        return vision_sync_report_failure(
            task,
            failure_code,
            session_token,
            device_id,
            base_url
        );
    }
    if (s_provider.analyze == NULL) {
        // No on-device recognizer in this image: report the task as
        // unsupported rather than fabricating a recognition result.
        (void)camera_vision_fail("unsupported");
        return vision_sync_report_failure(
            task,
            "unsupported",
            session_token,
            device_id,
            base_url
        );
    }
    vision_sync_result_t result = {};
    const esp_err_t analyze_result = s_provider.analyze(
        task->task_id,
        task->task_type,
        task->prompt,
        s_provider.context,
        &result
    );
    if (analyze_result != ESP_OK ||
        !vision_sync_core_result_is_valid(&result)) {
        (void)camera_vision_fail(
            vision_sync_failure_code_for_error(analyze_result)
        );
        return vision_sync_report_failure(
            task,
            vision_sync_failure_code_for_error(analyze_result),
            session_token,
            device_id,
            base_url
        );
    }
    if (camera_vision_acknowledge() != CAMERA_VISION_OK) {
        (void)camera_vision_fail("capture_failed");
        return vision_sync_report_failure(
            task,
            "capture_failed",
            session_token,
            device_id,
            base_url
        );
    }
    const esp_err_t report_result = vision_sync_report_result(
        task,
        &result,
        session_token,
        device_id,
        base_url
    );
    if (report_result == ESP_OK) {
        (void)camera_vision_complete();
    } else {
        (void)camera_vision_fail("model_rejected");
    }
    return report_result;
}

esp_err_t vision_sync_register_provider(const vision_sync_provider_t *provider) {
    if (provider == NULL) {
        memset(&s_provider, 0, sizeof(s_provider));
        return ESP_OK;
    }
    if (provider->analyze == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_provider = *provider;
    return ESP_OK;
}

esp_err_t vision_sync_now(void) {
    if (!camera_vision_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    vision_sync_task_t tasks[CONFIG_VISION_SYNC_MAX_TASKS_PER_CYCLE] = {};
    size_t task_count = 0;
    const esp_err_t fetch_result = vision_sync_fetch_tasks(
        tasks,
        CONFIG_VISION_SYNC_MAX_TASKS_PER_CYCLE,
        &task_count
    );
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }
    if (task_count == 0u) {
        return ESP_OK;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[VISION_SYNC_URL_SIZE] = {0};
    if (vision_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t last_result = ESP_OK;
    for (size_t index = 0; index < task_count; ++index) {
        last_result = vision_sync_run_task(
            &tasks[index],
            session_token,
            device_id,
            base_url
        );
        if (last_result != ESP_OK) {
            break;
        }
    }
    memset(session_token, 0, sizeof(session_token));
    return last_result;
}

static void vision_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = vision_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "vision task sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_VISION_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_VISION_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t vision_sync_initialize(void) {
    if (!camera_vision_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            vision_sync_worker,
            "vision_sync",
            6144,
            NULL,
            2,
            &s_task_handle
        ) != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    return ESP_OK;
}

const module_descriptor_t *vision_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "vision_sync",
        .version = "1.0.0",
        .initialize = vision_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
