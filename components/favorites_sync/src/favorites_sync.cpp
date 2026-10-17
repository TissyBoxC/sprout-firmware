#include "favorites_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "content_library.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "favorites.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "time_sync.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define FAVORITES_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define FAVORITES_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_FAVORITES_SYNC_INTERVAL_SECONDS
#define CONFIG_FAVORITES_SYNC_INTERVAL_SECONDS 900
#endif
#ifndef CONFIG_FAVORITES_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_FAVORITES_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define FAVORITES_SYNC_URL_SIZE 384
#define FAVORITES_SYNC_RESPONSE_SIZE 16384

static const char *const TAG = "favorites_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} favorites_sync_response_t;

static esp_err_t favorites_sync_response_init(
    favorites_sync_response_t *response
) {
    response->body = (char *)malloc(FAVORITES_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = FAVORITES_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void favorites_sync_response_destroy(
    favorites_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t favorites_sync_http_event(esp_http_client_event_t *event) {
    favorites_sync_response_t *response =
        (favorites_sync_response_t *)event->user_data;
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

static esp_err_t favorites_sync_fetch(JsonDocument *document) {
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
    char base_url[FAVORITES_SYNC_URL_SIZE] = {0};
    if (device_binding_client_get_platform_base_url(
            base_url,
            sizeof(base_url)
        ) != ESP_OK ||
        strncmp(base_url, "https://", 8) != 0) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
    char url[FAVORITES_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/content/favorites",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if FAVORITES_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    favorites_sync_response_t response = {};
    if (favorites_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = favorites_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        favorites_sync_response_destroy(&response);
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
        favorites_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        favorites_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        favorites_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        favorites_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    favorites_sync_response_destroy(&response);
    return ESP_OK;
}

static esp_err_t favorites_sync_apply_document(JsonDocument &document) {
    JsonArrayConst favorites = document["data"]["favorites"].as<JsonArrayConst>();
    if (favorites.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *package_ids[FAVORITES_CAPACITY] = {};
    size_t package_id_count = 0;
    size_t entry_count = 0;
    for (JsonVariantConst value : favorites) {
        entry_count += 1;
        if (package_id_count >= FAVORITES_CAPACITY) {
            continue;
        }
        const char *package_id = value["package_id"] | "";
        if (package_id[0] == '\0' ||
            strlen(package_id) >= CONTENT_LIBRARY_ID_SIZE) {
            continue;
        }
        package_ids[package_id_count] = package_id;
        package_id_count += 1;
    }
    if (package_id_count == 0 && entry_count > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return favorites_replace(package_ids, package_id_count);
}

esp_err_t favorites_sync_now(void) {
    if (!favorites_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    JsonDocument document;
    const esp_err_t fetch_result = favorites_sync_fetch(&document);
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }
    const esp_err_t apply_result = favorites_sync_apply_document(document);
    if (apply_result != ESP_OK) {
        ESP_LOGW(TAG, "favourites response rejected: %s",
                 esp_err_to_name(apply_result));
    }
    return apply_result;
}

static void favorites_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = favorites_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "favourites sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_FAVORITES_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_FAVORITES_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t favorites_sync_initialize(void) {
    if (!favorites_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            favorites_sync_worker,
            "favorites_sync",
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

const module_descriptor_t *favorites_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "favorites_sync",
        .version = "1.0.0",
        .initialize = favorites_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef FAVORITES_SYNC_HOST_TESTS
extern "C" {

bool favorites_sync_host_is_ready(void) {
    return s_ready;
}

void favorites_sync_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
}

void favorites_sync_host_set_stop(bool stop) {
    s_stop = stop;
}

}
#endif
