#include "touch_report.h"

#include <stdbool.h>
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
#include "sdkconfig.h"
#include "time_sync.h"
#include "touch_input.h"
#include "touch_report_core.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define TOUCH_REPORT_HAS_TRANSPORT_SECURITY 1
#else
#define TOUCH_REPORT_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_TOUCH_REPORT_INTERVAL_SECONDS
#define CONFIG_TOUCH_REPORT_INTERVAL_SECONDS 900
#endif
#ifndef CONFIG_TOUCH_REPORT_RETRY_INTERVAL_SECONDS
#define CONFIG_TOUCH_REPORT_RETRY_INTERVAL_SECONDS 15
#endif

#define TOUCH_REPORT_URL_SIZE 384
#define TOUCH_REPORT_RESPONSE_SIZE 4096
#define TOUCH_REPORT_REQUEST_SIZE 320

static const char *const TAG = "touch_report";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} touch_report_response_t;

static esp_err_t touch_report_response_init(touch_report_response_t *response) {
    response->body = (char *)malloc(TOUCH_REPORT_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = TOUCH_REPORT_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void touch_report_response_destroy(touch_report_response_t *response) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t touch_report_http_event(esp_http_client_event_t *event) {
    touch_report_response_t *response =
        (touch_report_response_t *)event->user_data;
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

static esp_err_t touch_report_prepare(
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

esp_err_t touch_report_now(void) {
    if (!touch_input_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[TOUCH_REPORT_URL_SIZE] = {0};
    if (touch_report_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[TOUCH_REPORT_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/touch/status",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if TOUCH_REPORT_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    const touch_input_config_t config = touch_input_get();
    const touch_status_t status = touch_report_build(&config);

    JsonDocument payload;
    payload["touch_supported"] = status.touch_supported;
    payload["haptic_supported"] = status.haptic_supported;
    payload["gesture_count"] = status.gesture_count;
    payload["policy_revision"] = config.revision;
    char request_body[TOUCH_REPORT_REQUEST_SIZE] = {0};
    const size_t body_length =
        serializeJson(payload, request_body, sizeof(request_body));
    if (body_length == 0 || body_length >= sizeof(request_body)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }

    touch_report_response_t response = {};
    if (touch_report_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config_client = {};
    config_client.url = url;
    config_client.method = HTTP_METHOD_POST;
    config_client.timeout_ms = 15000;
    config_client.event_handler = touch_report_http_event;
    config_client.user_data = &response;
    config_client.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config_client);
    if (client == NULL) {
        touch_report_response_destroy(&response);
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
    esp_http_client_set_post_field(client, request_body, (int)body_length);
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    memset(session_token, 0, sizeof(session_token));
    touch_report_response_destroy(&response);

    if (transport_result != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "touch status rejected with status %d", status_code);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void touch_report_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = touch_report_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "touch report failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_TOUCH_REPORT_RETRY_INTERVAL_SECONDS
            : CONFIG_TOUCH_REPORT_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t touch_report_initialize(void) {
    if (!touch_input_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            touch_report_worker,
            "touch_report",
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

const module_descriptor_t *touch_report_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "touch_report",
        .version = "1.0.0",
        .initialize = touch_report_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
