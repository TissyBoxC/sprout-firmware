#include "playback_history_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
#include "playback_history.h"
#include "sdkconfig.h"
#include "time_sync.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define PLAYBACK_HISTORY_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define PLAYBACK_HISTORY_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_PLAYBACK_HISTORY_SYNC_INTERVAL_SECONDS
#define CONFIG_PLAYBACK_HISTORY_SYNC_INTERVAL_SECONDS 120
#endif
#ifndef CONFIG_PLAYBACK_HISTORY_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_PLAYBACK_HISTORY_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define PLAYBACK_HISTORY_SYNC_URL_SIZE 384
#define PLAYBACK_HISTORY_SYNC_REQUEST_SIZE 256

static const char *const TAG = "playback_history_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

// Resolve the session token, device id and platform base url. The caller owns
// and must clear the token.
static esp_err_t playback_history_sync_prepare(
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

// Report one playback record to the platform. Returns ESP_OK once the platform
// accepts it so the caller can drop it from the queue. A 404 means the package
// no longer exists and is reported as ESP_ERR_NOT_FOUND so the caller can
// discard the stale row instead of retrying it forever; any other result keeps
// the row queued.
static esp_err_t playback_history_sync_report_record(
    const playback_record_t *record,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[PLAYBACK_HISTORY_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/content/playback",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
#if PLAYBACK_HISTORY_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif

    JsonDocument payload;
    payload["package_id"] = record->package_id;
    payload["package_version"] = (int)record->package_version;
    payload["outcome"] = playback_outcome_name(record->outcome);
    payload["played_seconds"] = (int)record->played_seconds;
    char request_body[PLAYBACK_HISTORY_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length =
        serializeJson(payload, request_body, sizeof(request_body));
    if (body_length == 0 || body_length >= sizeof(request_body)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
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
    if (transport_result != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 404) {
        return ESP_ERR_NOT_FOUND;
    }
    if (status_code < 200 || status_code >= 300) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Drain the queued playback records, stopping at the first transient failure so
// the rest of the queue survives for the next cycle.
static esp_err_t playback_history_sync_report_pending(void) {
    if (playback_history_pending_count() == 0) {
        return ESP_OK;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[PLAYBACK_HISTORY_SYNC_URL_SIZE] = {0};
    const esp_err_t prepare_result = playback_history_sync_prepare(
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
    playback_record_t records[PLAYBACK_HISTORY_OUTBOUND_CAPACITY] = {};
    size_t record_count = 0;
    const esp_err_t list_result = playback_history_list_pending(
            records,
            PLAYBACK_HISTORY_OUTBOUND_CAPACITY,
            &record_count
        );
    if (list_result != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return list_result;
    }
    esp_err_t result = ESP_OK;
    for (size_t index = 0; index < record_count; ++index) {
        const esp_err_t report_result = playback_history_sync_report_record(
            &records[index],
            session_token,
            device_id,
            base_url
        );
        if (report_result == ESP_OK || report_result == ESP_ERR_NOT_FOUND) {
            // A stale row for a removed package is dropped rather than retried
            // forever.
            result = playback_history_ack_pending(
                records[index].package_id,
                records[index].played_at
            );
            if (result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "playback history ack failed for %s: %s",
                    records[index].package_id,
                    esp_err_to_name(result)
                );
                break;
            }
        } else {
            ESP_LOGW(
                TAG,
                "playback history report failed for %s: %s",
                records[index].package_id,
                esp_err_to_name(report_result)
            );
            result = report_result;
            break;
        }
    }
    memset(session_token, 0, sizeof(session_token));
    return result;
}

esp_err_t playback_history_sync_now(void) {
    if (!playback_history_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    return playback_history_sync_report_pending();
}

static void playback_history_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = playback_history_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "playback history sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_PLAYBACK_HISTORY_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_PLAYBACK_HISTORY_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t playback_history_sync_initialize(void) {
    if (!playback_history_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            playback_history_sync_worker,
            "playback_hist_sync",
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

const module_descriptor_t *playback_history_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "playback_history_sync",
        .version = "1.0.0",
        .initialize = playback_history_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef PLAYBACK_HISTORY_SYNC_HOST_TESTS
extern "C" {

bool playback_history_sync_host_is_ready(void) {
    return s_ready;
}

void playback_history_sync_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
}

void playback_history_sync_host_set_stop(bool stop) {
    s_stop = stop;
}

}
#endif
