#include "playback_request_sync.h"

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
#include "playback_request.h"
#include "sdkconfig.h"
#include "time_sync.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define PLAYBACK_REQUEST_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define PLAYBACK_REQUEST_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_PLAYBACK_REQUEST_SYNC_INTERVAL_SECONDS
#define CONFIG_PLAYBACK_REQUEST_SYNC_INTERVAL_SECONDS 60
#endif
#ifndef CONFIG_PLAYBACK_REQUEST_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_PLAYBACK_REQUEST_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define PLAYBACK_REQUEST_SYNC_URL_SIZE 384
#define PLAYBACK_REQUEST_SYNC_RESPONSE_SIZE 32768

static const char *const TAG = "playback_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} playback_request_sync_response_t;

static esp_err_t playback_request_sync_response_init(
    playback_request_sync_response_t *response
) {
    response->body = (char *)malloc(PLAYBACK_REQUEST_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = PLAYBACK_REQUEST_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void playback_request_sync_response_destroy(
    playback_request_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t playback_request_sync_http_event(
    esp_http_client_event_t *event
) {
    playback_request_sync_response_t *response =
        (playback_request_sync_response_t *)event->user_data;
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

static esp_err_t playback_request_sync_fetch(JsonDocument *document) {
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!time_sync_is_synchronized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!device_binding_client_is_registered()) {
        return ESP_ERR_INVALID_STATE;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    if (device_binding_client_copy_session_token(
            session_token,
            sizeof(session_token)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    if (device_identity_copy(device_id, sizeof(device_id)) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_STATE;
    }
    char base_url[PLAYBACK_REQUEST_SYNC_URL_SIZE] = {0};
    if (device_binding_client_get_platform_base_url(
            base_url,
            sizeof(base_url)
        ) != ESP_OK ||
        strncmp(base_url, "https://", 8) != 0) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
    char url[PLAYBACK_REQUEST_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/playback/requests",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if PLAYBACK_REQUEST_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    playback_request_sync_response_t response = {};
    if (playback_request_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = playback_request_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        playback_request_sync_response_destroy(&response);
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
        playback_request_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        playback_request_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        playback_request_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        playback_request_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    playback_request_sync_response_destroy(&response);
    return ESP_OK;
}

// Copy one JSON string into a fixed-size destination, keeping the field empty
// when the value is missing or too long to hold safely.
static void playback_request_sync_copy_field(
    char *destination,
    size_t destination_size,
    const char *value
) {
    if (destination_size == 0) {
        return;
    }
    destination[0] = '\0';
    if (value == NULL) {
        return;
    }
    const size_t length = strlen(value);
    if (length == 0 || length >= destination_size) {
        return;
    }
    memcpy(destination, value, length + 1);
}

static esp_err_t playback_request_sync_apply_document(JsonDocument &document) {
    JsonArrayConst requests = document["data"]["requests"].as<JsonArrayConst>();
    if (requests.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    playback_request_t parsed[PLAYBACK_REQUEST_CAPACITY] = {};
    size_t request_count = 0;
    for (JsonVariantConst value : requests) {
        if (request_count >= PLAYBACK_REQUEST_CAPACITY) {
            break;
        }
        playback_request_t request = {};
        playback_request_sync_copy_field(
            request.request_id,
            sizeof(request.request_id),
            value["id"] | ""
        );
        playback_request_sync_copy_field(
            request.package_id,
            sizeof(request.package_id),
            value["package_id"] | ""
        );
        playback_request_sync_copy_field(
            request.note,
            sizeof(request.note),
            value["note"] | ""
        );
        playback_request_status_t status = PLAYBACK_REQUEST_PENDING;
        if (request.request_id[0] == '\0' || request.package_id[0] == '\0' ||
            !playback_request_status_from_name(
                value["status"] | "",
                &status
            )) {
            continue;
        }
        request.status = status;
        request.outcome = PLAYBACK_REQUEST_OUTCOME_NONE;
        parsed[request_count] = request;
        request_count += 1;
    }
    return playback_request_merge_remote(parsed, request_count);
}

esp_err_t playback_request_sync_now(void) {
    if (!playback_request_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    JsonDocument document;
    const esp_err_t fetch_result = playback_request_sync_fetch(&document);
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }
    const esp_err_t apply_result =
        playback_request_sync_apply_document(document);
    if (apply_result != ESP_OK) {
        ESP_LOGW(TAG, "playback request response rejected: %s",
                 esp_err_to_name(apply_result));
    }
    return apply_result;
}

static void playback_request_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = playback_request_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "playback request sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_PLAYBACK_REQUEST_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_PLAYBACK_REQUEST_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t playback_request_sync_initialize(void) {
    if (!playback_request_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            playback_request_sync_worker,
            "playback_sync",
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

const module_descriptor_t *playback_request_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "playback_request_sync",
        .version = "1.0.0",
        .initialize = playback_request_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
