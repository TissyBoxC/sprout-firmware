#include "offline_policy_sync.h"

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
#include "network_manager.h"
#include "offline_policy.h"
#include "sdkconfig.h"
#include "time_sync.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define OFFLINE_POLICY_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define OFFLINE_POLICY_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_OFFLINE_POLICY_SYNC_INTERVAL_SECONDS
#define CONFIG_OFFLINE_POLICY_SYNC_INTERVAL_SECONDS 1800
#endif
#ifndef CONFIG_OFFLINE_POLICY_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_OFFLINE_POLICY_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define OFFLINE_POLICY_SYNC_URL_SIZE 384
#define OFFLINE_POLICY_SYNC_RESPONSE_SIZE 4096
#define OFFLINE_POLICY_SYNC_REQUEST_SIZE 128

static const char *const TAG = "offline_policy_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} offline_policy_sync_response_t;

static esp_err_t offline_policy_sync_response_init(
    offline_policy_sync_response_t *response
) {
    response->body = (char *)malloc(OFFLINE_POLICY_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = OFFLINE_POLICY_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void offline_policy_sync_response_destroy(
    offline_policy_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t offline_policy_sync_http_event(
    esp_http_client_event_t *event
) {
    offline_policy_sync_response_t *response =
        (offline_policy_sync_response_t *)event->user_data;
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

// Fill the shared request prerequisites: a connected network, a synchronized
// clock, a registered device, and an authenticated session against an HTTPS
// platform endpoint. Returns ESP_ERR_INVALID_STATE when a caller should retry
// later, or ESP_ERR_INVALID_ARG for a misconfigured endpoint.
static esp_err_t offline_policy_sync_prepare(
    char *session_token,
    size_t session_token_size,
    char *device_id,
    size_t device_id_size,
    char *base_url,
    size_t base_url_size
) {
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!time_sync_is_synchronized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!device_binding_client_is_registered()) {
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

static esp_err_t offline_policy_sync_fetch(JsonDocument *document) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[OFFLINE_POLICY_SYNC_URL_SIZE] = {0};
    const esp_err_t prepared = offline_policy_sync_prepare(
        session_token,
        sizeof(session_token),
        device_id,
        sizeof(device_id),
        base_url,
        sizeof(base_url)
    );
    if (prepared != ESP_OK) {
        return prepared;
    }
    char url[OFFLINE_POLICY_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/content/offline-policy",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if OFFLINE_POLICY_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    offline_policy_sync_response_t response = {};
    if (offline_policy_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = offline_policy_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        offline_policy_sync_response_destroy(&response);
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
        offline_policy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        offline_policy_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        offline_policy_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        offline_policy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    offline_policy_sync_response_destroy(&response);
    return ESP_OK;
}

static esp_err_t offline_policy_sync_apply_document(JsonDocument &document) {
    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    // The cache byte cap is the field that actually bounds flash use, so a
    // response that omits it is rejected rather than guessed at.
    JsonVariantConst max_cache_bytes_value = data["max_cache_bytes"];
    if (!max_cache_bytes_value.is<int64_t>() &&
        !max_cache_bytes_value.is<uint64_t>() &&
        !max_cache_bytes_value.is<int>()) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t max_cache_bytes = max_cache_bytes_value.as<int64_t>();
    if (max_cache_bytes < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    // Start from the applied policy so an absent optional field keeps its
    // current value instead of silently flipping to a zero default.
    offline_policy_t policy = offline_policy_get();
    if (data["offline_enabled"].is<bool>()) {
        policy.offline_enabled = data["offline_enabled"].as<bool>();
    }
    if (data["download_over_wifi_only"].is<bool>()) {
        policy.download_over_wifi_only =
            data["download_over_wifi_only"].as<bool>();
    }
    if (data["max_cached_packages"].is<int>()) {
        policy.max_cached_packages =
            (uint16_t)data["max_cached_packages"].as<int>();
    }
    policy.max_cache_bytes = max_cache_bytes;
    if (data["cache_ttl_hours"].is<int>()) {
        policy.cache_ttl_hours =
            (uint16_t)data["cache_ttl_hours"].as<int>();
    }
    if (data["revision"].is<int>() ||
        data["revision"].is<int64_t>() ||
        data["revision"].is<uint64_t>()) {
        policy.revision = (uint32_t)data["revision"].as<uint32_t>();
    }
    return offline_policy_set(&policy);
}

// Report the applied revision so the console can see this device is up to date.
// The acknowledgement is best effort: the policy is already applied, so a
// failed report is logged without failing the sync.
static void offline_policy_sync_acknowledge(uint32_t revision) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[OFFLINE_POLICY_SYNC_URL_SIZE] = {0};
    if (offline_policy_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return;
    }
    char url[OFFLINE_POLICY_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/content/offline-policy/acknowledgement",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }
    char request_body[OFFLINE_POLICY_SYNC_REQUEST_SIZE] = {0};
    const int body_written = snprintf(
        request_body,
        sizeof(request_body),
        "{\"revision\":%lu}",
        (unsigned long)revision
    );
    if (body_written <= 0 || (size_t)body_written >= sizeof(request_body)) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }
    offline_policy_sync_response_t response = {};
    if (offline_policy_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.event_handler = offline_policy_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        offline_policy_sync_response_destroy(&response);
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
        (int)strlen(request_body)
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
            "policy acknowledgement failed: %s (status %d)",
            esp_err_to_name(transport_result),
            status_code
        );
    }
    offline_policy_sync_response_destroy(&response);
}

esp_err_t offline_policy_sync_now(void) {
    if (!offline_policy_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    JsonDocument document;
    const esp_err_t fetch_result = offline_policy_sync_fetch(&document);
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }
    const esp_err_t apply_result =
        offline_policy_sync_apply_document(document);
    if (apply_result != ESP_OK) {
        ESP_LOGW(TAG, "offline policy response rejected: %s",
                 esp_err_to_name(apply_result));
        return apply_result;
    }
    // Confirm the applied revision so the console sees this device as current.
    const offline_policy_t applied = offline_policy_get();
    offline_policy_sync_acknowledge(applied.revision);
    return ESP_OK;
}

static void offline_policy_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = offline_policy_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "offline policy sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_OFFLINE_POLICY_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_OFFLINE_POLICY_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t offline_policy_sync_initialize(void) {
    if (!offline_policy_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            offline_policy_sync_worker,
            "offline_policy_sync",
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

const module_descriptor_t *offline_policy_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "offline_policy_sync",
        .version = "1.0.0",
        .initialize = offline_policy_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef OFFLINE_POLICY_SYNC_HOST_TESTS
extern "C" {

bool offline_policy_sync_host_is_ready(void) {
    return s_ready;
}

void offline_policy_sync_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
}

void offline_policy_sync_host_set_stop(bool stop) {
    s_stop = stop;
}

}
#endif
