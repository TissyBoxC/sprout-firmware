#include "motion_policy_sync.h"

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
#include "motion_control.h"
#include "motion_policy_sync_core.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "time_sync.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define MOTION_POLICY_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define MOTION_POLICY_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_MOTION_POLICY_SYNC_INTERVAL_SECONDS
#define CONFIG_MOTION_POLICY_SYNC_INTERVAL_SECONDS 1800
#endif
#ifndef CONFIG_MOTION_POLICY_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_MOTION_POLICY_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define MOTION_POLICY_SYNC_URL_SIZE 384
#define MOTION_POLICY_SYNC_RESPONSE_SIZE 4096

static const char *const TAG = "motion_policy_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} motion_policy_sync_response_t;

static esp_err_t motion_policy_sync_response_init(
    motion_policy_sync_response_t *response
) {
    response->body = (char *)malloc(MOTION_POLICY_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = MOTION_POLICY_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void motion_policy_sync_response_destroy(
    motion_policy_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t motion_policy_sync_http_event(
    esp_http_client_event_t *event
) {
    motion_policy_sync_response_t *response =
        (motion_policy_sync_response_t *)event->user_data;
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

static esp_err_t motion_policy_sync_prepare(
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

esp_err_t motion_policy_sync_now(void) {
    if (!motion_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[MOTION_POLICY_SYNC_URL_SIZE] = {0};
    if (motion_policy_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char url[MOTION_POLICY_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/motion/policy",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if MOTION_POLICY_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    motion_policy_sync_response_t response = {};
    if (motion_policy_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = motion_policy_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        motion_policy_sync_response_destroy(&response);
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
        motion_policy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        motion_policy_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        motion_policy_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed) {
        motion_policy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    if (deserializeJson(document, response.body) != DeserializationError::Ok) {
        motion_policy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    motion_policy_sync_response_destroy(&response);

    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    const motion_policy_t policy = motion_policy_sync_core_build(
        data["enabled"] | false,
        data["require_stop_ack"] | false,
        data["allow_locomotion"] | false,
        data["max_speed_percent"] | 0,
        data["max_runtime_ms"] | 0,
        data["stop_grace_ms"] | 0,
        data["revision"] | 0
    );
    return motion_set(&policy);
}

static void motion_policy_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = motion_policy_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "motion policy sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_MOTION_POLICY_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_MOTION_POLICY_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t motion_policy_sync_initialize(void) {
    if (!motion_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            motion_policy_sync_worker,
            "motion_pol_sync",
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

const module_descriptor_t *motion_policy_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "motion_policy_sync",
        .version = "1.0.0",
        .initialize = motion_policy_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
