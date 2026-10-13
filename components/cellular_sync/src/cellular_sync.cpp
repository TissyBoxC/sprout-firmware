#include "cellular_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "cellular_4g.h"
#include "cellular_sync_core.h"
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

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define CELLULAR_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define CELLULAR_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_CELLULAR_SYNC_INTERVAL_SECONDS
#define CONFIG_CELLULAR_SYNC_INTERVAL_SECONDS 900
#endif
#ifndef CONFIG_CELLULAR_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_CELLULAR_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define CELLULAR_SYNC_URL_SIZE 384
#define CELLULAR_SYNC_RESPONSE_SIZE 8192
#define CELLULAR_SYNC_REQUEST_SIZE 512

static const char *const TAG = "cellular_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;
static cellular_sync_provider_t s_provider;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} cellular_sync_response_t;

static esp_err_t cellular_sync_response_init(cellular_sync_response_t *response) {
    response->body = (char *)malloc(CELLULAR_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = CELLULAR_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void cellular_sync_response_destroy(cellular_sync_response_t *response) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t cellular_sync_http_event(esp_http_client_event_t *event) {
    cellular_sync_response_t *response =
        (cellular_sync_response_t *)event->user_data;
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

static esp_err_t cellular_sync_prepare(
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

static esp_err_t cellular_sync_request(
    const char *method,
    const char *url,
    const char *session_token,
    const char *request_body,
    cellular_sync_response_t *response
) {
#if CELLULAR_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif
    if (cellular_sync_response_init(response) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method =
        strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = cellular_sync_http_event;
    config.user_data = response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        cellular_sync_response_destroy(response);
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
        cellular_sync_response_destroy(response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        cellular_sync_response_destroy(response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        cellular_sync_response_destroy(response);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t cellular_sync_apply_policy(
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[CELLULAR_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/connectivity/policy",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    cellular_sync_response_t response = {};
    const esp_err_t request_result = cellular_sync_request(
        "GET",
        url,
        session_token,
        nullptr,
        &response
    );
    if (request_result != ESP_OK) {
        return request_result;
    }
    if (response.overflowed) {
        cellular_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    if (deserializeJson(document, response.body) != DeserializationError::Ok) {
        cellular_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    cellular_sync_response_destroy(&response);

    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    cellular_policy_t policy = {};
    if (!cellular_sync_core_parse_policy(
            data["schema_version"] | 0u,
            data["enabled"] | false,
            data["roaming_allowed"] | false,
            data["remote_wake_enabled"] | false,
            data["metered_upload_limit"] | false,
            data["preference"] | "",
            data["monthly_quota_mb"] | 0u,
            data["low_quota_warn_percent"] | 0u,
            data["keepalive_seconds"] | 0u,
            &policy
        )) {
        return ESP_ERR_INVALID_ARG;
    }
    return cellular_set(&policy);
}

static esp_err_t cellular_sync_report_status(
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[CELLULAR_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/connectivity/status",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    cellular_sync_status_t status = {
        .signal_percent = 0,
        .registered = false,
        .esim_state = CELLULAR_ESIM_ABSENT,
    };
    if (s_provider.read != NULL) {
        cellular_sync_status_t provided = {};
        if (s_provider.read(s_provider.context, &provided) == ESP_OK) {
            status = provided;
        }
    }
    if (status.signal_percent > 100u) {
        status.signal_percent = 100u;
    }
    const cellular_policy_t policy = cellular_get();
    JsonDocument payload;
    payload["bearer"] = policy.enabled ? "cellular" : "wifi";
    payload["roaming"] = false;
    payload["esim_state"] = cellular_sync_esim_state_name(status.esim_state);
    payload["signal_percent"] = (int)status.signal_percent;
    payload["used_mb"] = 0;
    payload["policy_revision"] = 0;
    char body[CELLULAR_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length = serializeJson(payload, body, sizeof(body));
    if (body_length == 0 || body_length >= sizeof(body)) {
        return ESP_ERR_INVALID_ARG;
    }
    cellular_sync_response_t response = {};
    const esp_err_t request_result = cellular_sync_request(
        "POST",
        url,
        session_token,
        body,
        &response
    );
    cellular_sync_response_destroy(&response);
    return request_result;
}

esp_err_t cellular_sync_register_provider(
    const cellular_sync_provider_t *provider
) {
    if (provider == NULL) {
        memset(&s_provider, 0, sizeof(s_provider));
        return ESP_OK;
    }
    if (provider->read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_provider = *provider;
    return ESP_OK;
}

esp_err_t cellular_sync_now(void) {
    if (!cellular_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[CELLULAR_SYNC_URL_SIZE] = {0};
    if (cellular_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t policy_result = cellular_sync_apply_policy(
        session_token,
        device_id,
        base_url
    );
    if (policy_result != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return policy_result;
    }
    const esp_err_t status_result = cellular_sync_report_status(
        session_token,
        device_id,
        base_url
    );
    memset(session_token, 0, sizeof(session_token));
    return status_result;
}

static void cellular_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = cellular_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "cellular sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_CELLULAR_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_CELLULAR_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t cellular_sync_initialize(void) {
    if (!cellular_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            cellular_sync_worker,
            "cellular_sync",
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

const module_descriptor_t *cellular_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "cellular_sync",
        .version = "1.0.0",
        .initialize = cellular_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
