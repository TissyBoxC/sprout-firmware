#include "bluetooth_audio_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "bluetooth_audio.h"
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
#define BLUETOOTH_AUDIO_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define BLUETOOTH_AUDIO_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_BLUETOOTH_AUDIO_SYNC_INTERVAL_SECONDS
#define CONFIG_BLUETOOTH_AUDIO_SYNC_INTERVAL_SECONDS 1800
#endif
#ifndef CONFIG_BLUETOOTH_AUDIO_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_BLUETOOTH_AUDIO_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define BLUETOOTH_AUDIO_SYNC_URL_SIZE 384
#define BLUETOOTH_AUDIO_SYNC_RESPONSE_SIZE 4096
#define BLUETOOTH_AUDIO_SYNC_REQUEST_SIZE 512

static const char *const TAG = "bluetooth_audio_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} bluetooth_audio_sync_response_t;

static esp_err_t bluetooth_audio_sync_response_init(
    bluetooth_audio_sync_response_t *response
) {
    response->body = (char *)malloc(BLUETOOTH_AUDIO_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = BLUETOOTH_AUDIO_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void bluetooth_audio_sync_response_destroy(
    bluetooth_audio_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t bluetooth_audio_sync_http_event(
    esp_http_client_event_t *event
) {
    bluetooth_audio_sync_response_t *response =
        (bluetooth_audio_sync_response_t *)event->user_data;
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
static esp_err_t bluetooth_audio_sync_prepare(
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

static esp_err_t bluetooth_audio_sync_fetch(JsonDocument *document) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[BLUETOOTH_AUDIO_SYNC_URL_SIZE] = {0};
    if (bluetooth_audio_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char url[BLUETOOTH_AUDIO_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/bluetooth-audio/policy",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if BLUETOOTH_AUDIO_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    bluetooth_audio_sync_response_t response = {};
    if (bluetooth_audio_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = bluetooth_audio_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        bluetooth_audio_sync_response_destroy(&response);
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
        bluetooth_audio_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        bluetooth_audio_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        bluetooth_audio_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        bluetooth_audio_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    bluetooth_audio_sync_response_destroy(&response);
    return ESP_OK;
}

static esp_err_t bluetooth_audio_sync_apply_document(JsonDocument &document) {
    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *preferred_codec = data["preferred_codec"] | "";
    if (preferred_codec[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    bluetooth_audio_remote_t remote = {};
    remote.enabled = data["enabled"] | false;
    remote.speaker_mode_allowed = data["speaker_mode_allowed"] | false;
    remote.pairing_allowed = data["pairing_allowed"] | false;
    remote.auto_reconnect = data["auto_reconnect"] | false;
    remote.preferred_codec = preferred_codec;
    remote.max_volume_percent = data["max_volume_percent"] | 0;
    remote.display_name = data["display_name"] | "";
    remote.revision = data["revision"] | 0;
    return bluetooth_audio_apply_remote(&remote);
}

// Report the applied policy and live adapter state so the platform can detect
// stale policy delivery. The caller propagates failures and retries.
static esp_err_t bluetooth_audio_sync_report_status(void) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[BLUETOOTH_AUDIO_SYNC_URL_SIZE] = {0};
    const esp_err_t prepare_result = bluetooth_audio_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        );
    if (prepare_result != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return prepare_result;
    }
    char url[BLUETOOTH_AUDIO_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/bluetooth-audio/status",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if BLUETOOTH_AUDIO_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    const bluetooth_audio_status_t status = bluetooth_audio_status();
    JsonDocument payload;
    payload["bluetooth_supported"] = status.supported;
    payload["speaker_mode"] = status.speaker_mode;
    payload["adapter_state"] =
        bluetooth_adapter_state_name(status.adapter_state);
    payload["paired_device_count"] = status.paired_device_count;
    payload["connected_device_count"] = status.connected_device_count;
    payload["active_codec"] =
        bluetooth_active_codec_name(status.active_codec);
    payload["volume_percent"] = status.volume_percent;
    payload["last_disconnect_reason"] =
        bluetooth_disconnect_reason_name(status.last_disconnect_reason);
    payload["policy_revision"] = (int)status.revision;
    char request_body[BLUETOOTH_AUDIO_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length =
        serializeJson(payload, request_body, sizeof(request_body));
    if (body_length == 0 || body_length >= sizeof(request_body)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }

    bluetooth_audio_sync_response_t response = {};
    if (bluetooth_audio_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.event_handler = bluetooth_audio_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        bluetooth_audio_sync_response_destroy(&response);
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
    bluetooth_audio_sync_response_destroy(&response);
    if (transport_result != ESP_OK) {
        return transport_result;
    }
    if (status_code == 401 || status_code == 403) {
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bluetooth_audio_sync_now(void) {
    if (!bluetooth_audio_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    JsonDocument document;
    const esp_err_t fetch_result = bluetooth_audio_sync_fetch(&document);
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }
    const esp_err_t apply_result =
        bluetooth_audio_sync_apply_document(document);
    if (apply_result != ESP_OK) {
        ESP_LOGW(TAG, "bluetooth audio policy rejected: %s",
                 esp_err_to_name(apply_result));
        return apply_result;
    }
    return bluetooth_audio_sync_report_status();
}

static void bluetooth_audio_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = bluetooth_audio_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "bluetooth audio policy sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_BLUETOOTH_AUDIO_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_BLUETOOTH_AUDIO_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t bluetooth_audio_sync_initialize(void) {
    if (!bluetooth_audio_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            bluetooth_audio_sync_worker,
            "bt_audio_sync",
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

const module_descriptor_t *bluetooth_audio_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "bluetooth_audio_sync",
        .version = "1.0.0",
        .initialize = bluetooth_audio_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef BLUETOOTH_AUDIO_SYNC_HOST_TESTS
extern "C" {

void bluetooth_audio_sync_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
}

void bluetooth_audio_sync_host_set_stop(bool stop) {
    s_stop = stop;
}

bool bluetooth_audio_sync_host_is_ready(void) {
    return s_ready;
}

}  // extern "C"
#endif
