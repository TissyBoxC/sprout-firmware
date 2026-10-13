#include "image_privacy_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "image_privacy.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "time_sync.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define IMAGE_PRIVACY_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define IMAGE_PRIVACY_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_IMAGE_PRIVACY_SYNC_INTERVAL_SECONDS
#define CONFIG_IMAGE_PRIVACY_SYNC_INTERVAL_SECONDS 3600
#endif
#ifndef CONFIG_IMAGE_PRIVACY_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_IMAGE_PRIVACY_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define IMAGE_PRIVACY_SYNC_URL_SIZE 384
#define IMAGE_PRIVACY_SYNC_RESPONSE_SIZE 4096
#define IMAGE_PRIVACY_SYNC_REQUEST_SIZE 640

static const char *const TAG = "image_privacy_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} image_privacy_sync_response_t;

static esp_err_t image_privacy_sync_response_init(
    image_privacy_sync_response_t *response
) {
    response->body = (char *)malloc(IMAGE_PRIVACY_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = IMAGE_PRIVACY_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void image_privacy_sync_response_destroy(
    image_privacy_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t image_privacy_sync_http_event(
    esp_http_client_event_t *event
) {
    image_privacy_sync_response_t *response =
        (image_privacy_sync_response_t *)event->user_data;
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

// Resolve the session token, device id and platform base url shared by the
// policy fetch and the status report. The caller owns and must clear the token.
static esp_err_t image_privacy_sync_prepare(
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

static esp_err_t image_privacy_sync_fetch(JsonDocument *document) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[IMAGE_PRIVACY_SYNC_URL_SIZE] = {0};
    if (image_privacy_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char url[IMAGE_PRIVACY_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/image-privacy/policy",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if IMAGE_PRIVACY_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    image_privacy_sync_response_t response = {};
    if (image_privacy_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = image_privacy_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        image_privacy_sync_response_destroy(&response);
        memset(session_token, 0, sizeof(session_token));
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
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    memset(session_token, 0, sizeof(session_token));

    if (transport_result != ESP_OK) {
        image_privacy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        image_privacy_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        image_privacy_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        image_privacy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    image_privacy_sync_response_destroy(&response);
    return ESP_OK;
}

static esp_err_t image_privacy_sync_apply_document(
    JsonDocument &document
) {
    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *processing_mode = data["processing_mode"] | "";
    const char *upload_scope = data["upload_scope"] | "";
    const char *redaction_level = data["redaction_level"] | "";
    if (processing_mode[0] == '\0' || upload_scope[0] == '\0' ||
        redaction_level[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    image_privacy_remote_t remote = {};
    remote.enabled = data["enabled"] | false;
    remote.upload_allowed = data["upload_allowed"] | false;
    remote.local_processing_only = data["local_processing_only"] | true;
    remote.processing_mode = processing_mode;
    remote.upload_scope = upload_scope;
    remote.retention_days = data["retention_days"] | 0;
    remote.redaction_level = redaction_level;
    remote.require_child_consent = data["require_child_consent"] | true;
    remote.revision = data["revision"] | 0;
    return image_privacy_apply_remote(&remote);
}

// The status report is best effort: the policy is already applied, so a failed
// report is logged without failing or delaying the sync cycle.
static void image_privacy_sync_report_status(void) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[IMAGE_PRIVACY_SYNC_URL_SIZE] = {0};
    if (image_privacy_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return;
    }
    char url[IMAGE_PRIVACY_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/image-privacy/status",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }
#if IMAGE_PRIVACY_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }
#endif

    const image_privacy_status_t status = image_privacy_status();
    JsonDocument payload;
    payload["camera_supported"] = status.camera_supported;
    payload["local_processing"] = status.local_processing;
    payload["upload_allowed"] = status.upload_allowed;
    payload["processing_mode"] =
        image_privacy_mode_name(status.processing_mode);
    payload["upload_scope"] =
        image_privacy_scope_name(status.upload_scope);
    payload["redaction_level"] =
        image_privacy_redaction_name(status.redaction_level);
    payload["processed_count"] = (int)status.processed_count;
    payload["uploaded_count"] = (int)status.uploaded_count;
    payload["blocked_count"] = (int)status.blocked_count;
    payload["last_block_reason"] =
        image_privacy_block_reason_name(status.last_block_reason);
    payload["policy_revision"] = (int)status.revision;
    char request_body[IMAGE_PRIVACY_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length =
        serializeJson(payload, request_body, sizeof(request_body));
    if (body_length == 0 || body_length >= sizeof(request_body)) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }

    image_privacy_sync_response_t response = {};
    if (image_privacy_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.event_handler = image_privacy_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        image_privacy_sync_response_destroy(&response);
        memset(session_token, 0, sizeof(session_token));
        return;
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
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(
        client,
        request_body,
        (int)body_length
    );
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    memset(session_token, 0, sizeof(session_token));
    if (transport_result != ESP_OK ||
        status_code < 200 || status_code >= 300) {
        ESP_LOGW(
            TAG,
            "image privacy status report failed: %s (status %d)",
            esp_err_to_name(transport_result),
            status_code
        );
    }
    image_privacy_sync_response_destroy(&response);
}

esp_err_t image_privacy_sync_now(void) {
    if (!image_privacy_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    JsonDocument document;
    const esp_err_t fetch_result = image_privacy_sync_fetch(&document);
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }
    const esp_err_t apply_result =
        image_privacy_sync_apply_document(document);
    if (apply_result != ESP_OK) {
        ESP_LOGW(TAG, "image privacy policy rejected: %s",
                 esp_err_to_name(apply_result));
        return apply_result;
    }
    image_privacy_sync_report_status();
    return ESP_OK;
}

static void image_privacy_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = image_privacy_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "image privacy policy sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_IMAGE_PRIVACY_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_IMAGE_PRIVACY_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t image_privacy_sync_initialize(void) {
    if (!image_privacy_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            image_privacy_sync_worker,
            "image_privacy_sync",
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

const module_descriptor_t *image_privacy_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "image_privacy_sync",
        .version = "1.0.0",
        .initialize = image_privacy_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
