#include "connectivity_policy_sync.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "config_store.h"
#include "connectivity_policy_sync_core.h"
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
#define CONNECTIVITY_POLICY_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define CONNECTIVITY_POLICY_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_CONNECTIVITY_POLICY_SYNC_INTERVAL_SECONDS
#define CONFIG_CONNECTIVITY_POLICY_SYNC_INTERVAL_SECONDS 900
#endif
#ifndef CONFIG_CONNECTIVITY_POLICY_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_CONNECTIVITY_POLICY_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define CONNECTIVITY_POLICY_SYNC_URL_SIZE 384
#define CONNECTIVITY_POLICY_SYNC_RESPONSE_SIZE 4096

// One bounded binary blob persists the applied connectivity policy so a reboot
// reports the same revision it last applied instead of falling back to zero.
#define CONNECTIVITY_POLICY_STORE_KEY "conn_policy"
CONFIG_STORE_KEY_ASSERT(CONNECTIVITY_POLICY_STORE_KEY);

static const char *const TAG = "connectivity_policy_sync";
static bool s_ready;
static bool s_initialized;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;
static connectivity_policy_state_t s_state;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} connectivity_policy_sync_response_t;

static esp_err_t connectivity_policy_sync_response_init(
    connectivity_policy_sync_response_t *response
) {
    response->body = (char *)malloc(CONNECTIVITY_POLICY_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = CONNECTIVITY_POLICY_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void connectivity_policy_sync_response_destroy(
    connectivity_policy_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t connectivity_policy_sync_http_event(
    esp_http_client_event_t *event
) {
    connectivity_policy_sync_response_t *response =
        (connectivity_policy_sync_response_t *)event->user_data;
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

static void connectivity_policy_sync_restore(void) {
    connectivity_policy_state_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        CONNECTIVITY_POLICY_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        restored.schema_version == CONNECTIVITY_POLICY_SCHEMA_VERSION) {
        s_state = restored;
        return;
    }
    s_state = connectivity_policy_state_default();
}

static esp_err_t connectivity_policy_sync_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        CONNECTIVITY_POLICY_STORE_KEY,
        &s_state,
        sizeof(s_state)
    );
    return stored == ESP_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

uint32_t connectivity_policy_applied_revision(void) {
    return s_state.revision;
}

static esp_err_t connectivity_policy_sync_prepare(
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

esp_err_t connectivity_policy_sync_now(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[CONNECTIVITY_POLICY_SYNC_URL_SIZE] = {0};
    if (connectivity_policy_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char url[CONNECTIVITY_POLICY_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/connectivity/policy",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if CONNECTIVITY_POLICY_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    connectivity_policy_sync_response_t response = {};
    if (connectivity_policy_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t http_config = {};
    http_config.url = url;
    http_config.method = HTTP_METHOD_GET;
    http_config.timeout_ms = 15000;
    http_config.event_handler = connectivity_policy_sync_http_event;
    http_config.user_data = &response;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        connectivity_policy_sync_response_destroy(&response);
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
        connectivity_policy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        connectivity_policy_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        connectivity_policy_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed) {
        connectivity_policy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    if (deserializeJson(document, response.body) != DeserializationError::Ok) {
        connectivity_policy_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    connectivity_policy_sync_response_destroy(&response);

    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *preference = "";
    if (!data["enabled"].is<bool>() ||
        !data["roaming_allowed"].is<bool>() ||
        !data["remote_wake_enabled"].is<bool>() ||
        !data["metered_upload_limit"].is<bool>() ||
        !data["preference"].is<const char *>() ||
        !data["revision"].is<int>()) {
        return ESP_ERR_INVALID_ARG;
    }
    preference = data["preference"].as<const char *>();
    const int revision = data["revision"].as<int>();
    if (preference == NULL || revision < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const connectivity_policy_state_t applied =
        connectivity_policy_sync_core_build(
            data["enabled"].as<bool>(),
            data["roaming_allowed"].as<bool>(),
            data["remote_wake_enabled"].as<bool>(),
            data["metered_upload_limit"].as<bool>(),
            preference,
            revision
        );
    s_state = applied;
    return connectivity_policy_sync_persist();
}

static void connectivity_policy_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = connectivity_policy_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "connectivity policy sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_CONNECTIVITY_POLICY_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_CONNECTIVITY_POLICY_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t connectivity_policy_sync_initialize(void) {
    if (!s_initialized) {
        connectivity_policy_sync_restore();
        s_initialized = true;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            connectivity_policy_sync_worker,
            "conn_pol_sync",
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

const module_descriptor_t *connectivity_policy_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "connectivity_policy_sync",
        .version = "1.0.0",
        .initialize = connectivity_policy_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
