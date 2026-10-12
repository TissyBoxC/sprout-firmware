#include "theme_package_sync.h"

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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "theme_package.h"
#include "time_sync.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define THEME_PACKAGE_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define THEME_PACKAGE_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_THEME_PACKAGE_SYNC_INTERVAL_SECONDS
#define CONFIG_THEME_PACKAGE_SYNC_INTERVAL_SECONDS 1800
#endif
#ifndef CONFIG_THEME_PACKAGE_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_THEME_PACKAGE_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define THEME_PACKAGE_SYNC_URL_SIZE 384
#define THEME_PACKAGE_SYNC_RESPONSE_SIZE 32768

static const char *const TAG = "theme_package_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} theme_package_sync_response_t;

static esp_err_t theme_package_sync_response_init(
    theme_package_sync_response_t *response
) {
    response->body = (char *)malloc(THEME_PACKAGE_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = THEME_PACKAGE_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void theme_package_sync_response_destroy(
    theme_package_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t theme_package_sync_http_event(
    esp_http_client_event_t *event
) {
    theme_package_sync_response_t *response =
        (theme_package_sync_response_t *)event->user_data;
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

static esp_err_t theme_package_sync_fetch(JsonDocument *document) {
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
    char base_url[THEME_PACKAGE_SYNC_URL_SIZE] = {0};
    if (device_binding_client_get_platform_base_url(
            base_url,
            sizeof(base_url)
        ) != ESP_OK ||
        strncmp(base_url, "https://", 8) != 0) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
    char url[THEME_PACKAGE_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/content/theme-packs",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if THEME_PACKAGE_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    theme_package_sync_response_t response = {};
    if (theme_package_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = theme_package_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        theme_package_sync_response_destroy(&response);
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
        theme_package_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        theme_package_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        theme_package_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        theme_package_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    theme_package_sync_response_destroy(&response);
    return ESP_OK;
}

// Copy one JSON string field into a fixed-size destination, keeping the field
// empty when the value is missing or too long to hold safely.
static void theme_package_sync_copy_field(
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

static esp_err_t theme_package_sync_apply_document(JsonDocument &document) {
    JsonArrayConst packs = document["data"]["theme_packs"]
        .as<JsonArrayConst>();
    if (packs.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    theme_pack_t parsed[THEME_PACKAGE_CAPACITY] = {};
    size_t pack_count = 0;
    size_t entry_count = 0;
    for (JsonVariantConst value : packs) {
        entry_count += 1;
        if (pack_count >= THEME_PACKAGE_CAPACITY) {
            continue;
        }
        theme_pack_t pack = {};
        theme_package_sync_copy_field(
            pack.theme_id,
            sizeof(pack.theme_id),
            value["theme_id"] | ""
        );
        theme_package_sync_copy_field(
            pack.title,
            sizeof(pack.title),
            value["title"] | ""
        );
        if (pack.theme_id[0] == '\0' || pack.title[0] == '\0') {
            continue;
        }
        JsonArrayConst members = value["package_ids"].as<JsonArrayConst>();
        if (!members.isNull()) {
            for (JsonVariantConst member : members) {
                if (pack.member_count >= THEME_PACKAGE_MEMBER_CAPACITY) {
                    break;
                }
                const char *member_id = member.as<const char *>();
                if (member_id == NULL || member_id[0] == '\0' ||
                    strlen(member_id) >= CONTENT_LIBRARY_ID_SIZE) {
                    continue;
                }
                strncpy(
                    pack.package_ids[pack.member_count],
                    member_id,
                    CONTENT_LIBRARY_ID_SIZE - 1
                );
                pack.member_count += 1;
            }
        }
        if (pack.member_count == 0) {
            continue;
        }
        pack.catalog_revision = value["catalog_revision"] | (int64_t)0;
        parsed[pack_count] = pack;
        pack_count += 1;
    }
    if (pack_count == 0 && entry_count > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return theme_package_replace(parsed, pack_count);
}

esp_err_t theme_package_sync_now(void) {
    if (!theme_package_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    JsonDocument document;
    const esp_err_t fetch_result = theme_package_sync_fetch(&document);
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }
    const esp_err_t apply_result =
        theme_package_sync_apply_document(document);
    if (apply_result != ESP_OK) {
        ESP_LOGW(TAG, "theme pack response rejected: %s",
                 esp_err_to_name(apply_result));
    }
    return apply_result;
}

static void theme_package_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = theme_package_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "theme pack sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_THEME_PACKAGE_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_THEME_PACKAGE_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t theme_package_sync_initialize(void) {
    if (!theme_package_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            theme_package_sync_worker,
            "theme_sync",
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

const module_descriptor_t *theme_package_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "theme_package_sync",
        .version = "1.0.0",
        .initialize = theme_package_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
