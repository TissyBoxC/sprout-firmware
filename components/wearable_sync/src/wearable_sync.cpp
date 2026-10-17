#include "wearable_sync.h"

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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "time_sync.h"
#include "wearable_location.h"
#include "wearable_sync_core.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define WEARABLE_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define WEARABLE_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_WEARABLE_SYNC_INTERVAL_SECONDS
#define CONFIG_WEARABLE_SYNC_INTERVAL_SECONDS 300
#endif
#ifndef CONFIG_WEARABLE_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_WEARABLE_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define WEARABLE_SYNC_URL_SIZE 384
#define WEARABLE_SYNC_RESPONSE_SIZE 8192
#define WEARABLE_SYNC_REQUEST_SIZE 512

static const char *const TAG = "wearable_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;
static wearable_sync_provider_t s_provider;
static bool s_sos_reported;
static bool s_location_reported;
static int64_t s_last_location_upload_ms;
static int64_t s_last_sos_attempt_ms;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} wearable_sync_response_t;

static bool wearable_sync_policy_equal(
    const location_policy_t *left,
    const location_policy_t *right
) {
    return left->schema_version == right->schema_version &&
        left->enabled == right->enabled &&
        left->permission_granted == right->permission_granted &&
        left->precision == right->precision &&
        left->upload_interval_seconds == right->upload_interval_seconds &&
        left->retention_days == right->retention_days &&
        left->geofence_radius_meters == right->geofence_radius_meters;
}

static esp_err_t wearable_sync_response_init(wearable_sync_response_t *response) {
    response->body = (char *)malloc(WEARABLE_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = WEARABLE_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void wearable_sync_response_destroy(wearable_sync_response_t *response) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t wearable_sync_http_event(esp_http_client_event_t *event) {
    wearable_sync_response_t *response =
        (wearable_sync_response_t *)event->user_data;
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

static esp_err_t wearable_sync_prepare(
    char *session_token,
    size_t session_token_size,
    char *device_id,
    size_t device_id_size,
    char *base_url,
    size_t base_url_size
) {
    memset(session_token, 0, session_token_size);
    memset(device_id, 0, device_id_size);
    memset(base_url, 0, base_url_size);
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED ||
        !time_sync_is_synchronized() ||
        !device_binding_client_is_registered()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t session_result = device_binding_client_copy_session_token(
            session_token,
            session_token_size
        );
    if (session_result != ESP_OK) {
        return session_result;
    }
    const esp_err_t identity_result = device_identity_copy(
        device_id,
        device_id_size
    );
    if (identity_result != ESP_OK) {
        memset(session_token, 0, session_token_size);
        return identity_result;
    }
    const esp_err_t base_url_result =
        device_binding_client_get_platform_base_url(
            base_url,
            base_url_size
        );
    if (base_url_result != ESP_OK) {
        memset(session_token, 0, session_token_size);
        return base_url_result;
    }
    if (strncmp(base_url, "https://", 8) != 0) {
        memset(session_token, 0, session_token_size);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t wearable_sync_request(
    const char *method,
    const char *url,
    const char *session_token,
    const char *request_body,
    wearable_sync_response_t *response
) {
#if WEARABLE_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif
    if (wearable_sync_response_init(response) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method =
        strcmp(method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = wearable_sync_http_event;
    config.user_data = response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        wearable_sync_response_destroy(response);
        return ESP_ERR_NO_MEM;
    }
    char authorization[DEVICE_BINDING_SESSION_TOKEN_SIZE + 16] = {0};
    snprintf(
        authorization,
        sizeof(authorization),
        "Bearer %s",
        session_token
    );
    esp_err_t header_result = esp_http_client_set_header(
        client,
        "Authorization",
        authorization
    );
    if (header_result == ESP_OK) {
        header_result = esp_http_client_set_header(
            client,
            "Accept",
            "application/json"
        );
    }
    if (request_body != nullptr) {
        if (header_result == ESP_OK) {
            header_result = esp_http_client_set_header(
                client,
                "Content-Type",
                "application/json"
            );
        }
        if (header_result == ESP_OK) {
            header_result = esp_http_client_set_post_field(
                client,
                request_body,
                (int)strlen(request_body)
            );
        }
    }
    if (header_result != ESP_OK) {
        esp_http_client_cleanup(client);
        wearable_sync_response_destroy(response);
        memset(authorization, 0, sizeof(authorization));
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    if (transport_result != ESP_OK) {
        wearable_sync_response_destroy(response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        wearable_sync_response_destroy(response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        wearable_sync_response_destroy(response);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Apply the family policy. A revoked permission stops sampling and clears any
// latched SOS so a stale emergency cannot keep reporting a position.
static esp_err_t wearable_sync_apply_policy(
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[WEARABLE_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/wearable/policy",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    wearable_sync_response_t response = {};
    const esp_err_t request_result = wearable_sync_request(
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
        wearable_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    if (deserializeJson(document, response.body) != DeserializationError::Ok) {
        wearable_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    wearable_sync_response_destroy(&response);

    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!data["schema_version"].is<uint32_t>() ||
        !data["enabled"].is<bool>() ||
        !data["permission_granted"].is<bool>() ||
        !data["precision"].is<const char *>() ||
        !data["upload_interval_seconds"].is<uint32_t>() ||
        !data["retention_days"].is<uint32_t>() ||
        !data["geofence_radius_meters"].is<uint32_t>()) {
        return ESP_ERR_INVALID_ARG;
    }
    location_policy_t policy = {};
    if (!wearable_sync_core_parse_policy(
            data["schema_version"] | 0u,
            data["enabled"] | false,
            data["permission_granted"] | false,
            data["precision"] | "",
            data["upload_interval_seconds"] | 0u,
            data["retention_days"] | 0u,
            data["geofence_radius_meters"] | 0u,
            &policy
        )) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!policy.permission_granted) {
        const location_policy_t current = wearable_get();
        if (!current.permission_granted) {
            s_location_reported = false;
            s_last_location_upload_ms = 0;
            s_last_sos_attempt_ms = 0;
            return ESP_OK;
        }
        const esp_err_t revoke_result = wearable_revoke_permission();
        s_location_reported = false;
        s_last_location_upload_ms = 0;
        s_last_sos_attempt_ms = 0;
        return revoke_result == ESP_OK
            ? ESP_OK
            : ESP_ERR_INVALID_STATE;
    }
    const location_policy_t current = wearable_get();
    if (wearable_sync_policy_equal(&policy, &current)) {
        return ESP_OK;
    }
    const esp_err_t apply_result = wearable_set(&policy);
    return apply_result == ESP_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t wearable_sync_post(
    const char *url,
    const char *session_token,
    const char *body
) {
    wearable_sync_response_t response = {};
    const esp_err_t result = wearable_sync_request(
        "POST",
        url,
        session_token,
        body,
        &response
    );
    wearable_sync_response_destroy(&response);
    return result;
}

static esp_err_t wearable_sync_report_location(
    const location_policy_t *policy,
    const wearable_sync_position_t *position,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[WEARABLE_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/wearable/location",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument payload;
    payload["latitude"] = position->latitude;
    payload["longitude"] = position->longitude;
    payload["accuracy_meters"] = position->accuracy_meters;
    payload["precision"] =
        wearable_sync_precision_name((location_precision_t)policy->precision);
    // The platform evaluates the family geofence from the reported point, so
    // the device reports unknown rather than a locally guessed boundary state.
    payload["geofence_state"] = "unknown";
    char body[WEARABLE_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length = serializeJson(payload, body, sizeof(body));
    if (body_length == 0 || body_length >= sizeof(body)) {
        return ESP_ERR_INVALID_ARG;
    }
    return wearable_sync_post(url, session_token, body);
}

static esp_err_t wearable_sync_report_sos(
    const wearable_sync_position_t *position,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char url[WEARABLE_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/wearable/sos",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument payload;
    payload["latitude"] = position->latitude;
    payload["longitude"] = position->longitude;
    char body[WEARABLE_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length = serializeJson(payload, body, sizeof(body));
    if (body_length == 0 || body_length >= sizeof(body)) {
        return ESP_ERR_INVALID_ARG;
    }
    return wearable_sync_post(url, session_token, body);
}

esp_err_t wearable_sync_register_provider(
    const wearable_sync_provider_t *provider
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

static bool wearable_sync_location_due(const location_policy_t *policy) {
    if (!s_location_reported) {
        return true;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms < s_last_location_upload_ms) {
        s_location_reported = false;
        return true;
    }
    return location_should_upload(
        policy,
        (uint32_t)(now_ms - s_last_location_upload_ms)
    );
}

static bool wearable_sync_sos_due(void) {
    if (s_sos_reported) {
        return false;
    }
    if (s_last_sos_attempt_ms == 0) {
        return true;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms < s_last_sos_attempt_ms) {
        s_last_sos_attempt_ms = 0;
        return true;
    }
    return now_ms - s_last_sos_attempt_ms >=
        (int64_t)CONFIG_WEARABLE_SYNC_RETRY_INTERVAL_SECONDS * 1000;
}

esp_err_t wearable_sync_now(void) {
    if (!wearable_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[WEARABLE_SYNC_URL_SIZE] = {0};
    const esp_err_t prepare_result = wearable_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        );
    if (prepare_result != ESP_OK) {
        return prepare_result;
    }
    const esp_err_t policy_result = wearable_sync_apply_policy(
        session_token,
        device_id,
        base_url
    );
    if (policy_result != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return policy_result;
    }
    const location_policy_t policy = wearable_get();
    if (!location_can_sample(&policy)) {
        s_sos_reported = false;
        s_location_reported = false;
        s_last_location_upload_ms = 0;
        s_last_sos_attempt_ms = 0;
        memset(session_token, 0, sizeof(session_token));
        return ESP_OK;
    }
    if (s_provider.read == NULL) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_OK;
    }
    const bool location_due = wearable_sync_location_due(&policy);
    const bool sos_due =
        wearable_sos_active() && wearable_sync_sos_due();
    if (!location_due && !sos_due) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_OK;
    }
    wearable_sync_position_t position = {};
    if (s_provider.read(s_provider.context, &position) != ESP_OK ||
        !wearable_sync_core_position_is_valid(&position)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_OK;
    }
    esp_err_t result = ESP_OK;
    if (wearable_sos_active()) {
        // Report one triggered SOS exactly once so a latched emergency is not
        // duplicated on every poll. A failed attempt is retried independently
        // at the retry cadence instead of being hidden by a location success.
        if (sos_due) {
            s_last_sos_attempt_ms = esp_timer_get_time() / 1000;
            const esp_err_t sos_result = wearable_sync_report_sos(
                &position,
                session_token,
                device_id,
                base_url
            );
            if (sos_result == ESP_OK) {
                s_sos_reported = true;
            } else if (sos_result != ESP_ERR_INVALID_STATE) {
                result = sos_result;
            }
        }
    } else {
        s_sos_reported = false;
        s_last_sos_attempt_ms = 0;
    }
    if (location_due) {
        const esp_err_t location_result = wearable_sync_report_location(
            &policy,
            &position,
            session_token,
            device_id,
            base_url
        );
        if (location_result == ESP_OK) {
            s_location_reported = true;
            s_last_location_upload_ms = esp_timer_get_time() / 1000;
        } else if (result == ESP_OK) {
            result = location_result;
        }
    }
    memset(session_token, 0, sizeof(session_token));
    return result;
}

static void wearable_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = wearable_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "wearable sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_WEARABLE_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_WEARABLE_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t wearable_sync_initialize(void) {
    if (!wearable_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            wearable_sync_worker,
            "wearable_sync",
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

const module_descriptor_t *wearable_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "wearable_sync",
        .version = "1.0.0",
        .initialize = wearable_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef WEARABLE_SYNC_HOST_TESTS
extern "C" {

bool wearable_sync_host_is_ready(void) {
    return s_ready;
}

void wearable_sync_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
    s_sos_reported = false;
    s_location_reported = false;
    s_last_location_upload_ms = 0;
    s_last_sos_attempt_ms = 0;
    memset(&s_provider, 0, sizeof(s_provider));
}

void wearable_sync_host_set_stop(bool stop) {
    s_stop = stop;
}

}  // extern "C"
#endif
