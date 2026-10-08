// ArduinoJson is header-only and ESP-IDF supplies the TLS stack; this module
// stays in C++ so the payload builder remains type-safe.
#include "ArduinoJson.h"

#include "device_runtime_reporter.h"

extern "C" {

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cloud_auth.h"
#include "config_store.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "network_quality.h"
#include "offline_fallback.h"
#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER && __has_include("diagnostic_reporter.h")
#define DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER 1
#else
#define DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER 0
#endif
#if CONFIG_FEATURE_PROVISIONING_REPORTER && __has_include("provisioning_reporter.h")
#define DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER 1
#else
#define DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER 0
#endif

#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
#include "diagnostic_reporter.h"
#endif
#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
#include "provisioning_reporter.h"
#endif
#if CONFIG_FEATURE_PARENT_POLICY && __has_include("parent_policy.h")
#define DEVICE_RUNTIME_HAS_PARENT_POLICY 1
#else
#define DEVICE_RUNTIME_HAS_PARENT_POLICY 0
#endif

#if CONFIG_FEATURE_FACTORY_RESET && __has_include("factory_reset.h")
#define DEVICE_RUNTIME_HAS_FACTORY_RESET 1
#else
#define DEVICE_RUNTIME_HAS_FACTORY_RESET 0
#endif

#if DEVICE_RUNTIME_HAS_PARENT_POLICY
#include "parent_policy.h"
#endif
#if CONFIG_FEATURE_DEVICE_PROVISIONING && __has_include("device_provisioning.h")
#define DEVICE_RUNTIME_HAS_DEVICE_PROVISIONING 1
#include "device_provisioning.h"
#else
#define DEVICE_RUNTIME_HAS_DEVICE_PROVISIONING 0
#endif
#if DEVICE_RUNTIME_HAS_FACTORY_RESET
#include "factory_reset.h"
#endif
#include "time_sync.h"

#define DEVICE_RUNTIME_RESPONSE_SIZE 8192
#define DEVICE_RUNTIME_REQUEST_SIZE 16384
#define DEVICE_RUNTIME_URL_SIZE 320
#define DEVICE_RUNTIME_SESSION_TOKEN_SIZE DEVICE_BINDING_SESSION_TOKEN_SIZE
#define DEVICE_RUNTIME_HEARTBEAT_ID_SIZE 64
#define DEVICE_RUNTIME_COMMAND_ID_SIZE 64
#define DEVICE_RUNTIME_COMMAND_LIMIT 4
#define DEVICE_RUNTIME_COMMAND_ACK_PAYLOAD_SIZE 256
#define DEVICE_RUNTIME_FACTORY_RESET_COMPLETED_MAGIC 0x52535431u

static const char *const TAG = "device_runtime";

typedef struct {
    char *body;
    size_t body_capacity;
    size_t body_length;
    bool overflowed;
} device_runtime_response_buffer_t;

typedef struct {
    char id[DEVICE_RUNTIME_COMMAND_ID_SIZE];
    char type[32];
} device_runtime_command_t;

typedef enum {
    DEVICE_RUNTIME_COMMAND_ACK_OK = 0,
    DEVICE_RUNTIME_COMMAND_ACK_ALREADY_HANDLED,
    DEVICE_RUNTIME_COMMAND_ACK_RETRYABLE,
    DEVICE_RUNTIME_COMMAND_ACK_UNAUTHORIZED,
    DEVICE_RUNTIME_COMMAND_ACK_INVALID,
} device_runtime_command_ack_result_t;

typedef enum {
    DEVICE_RUNTIME_COMMAND_EXECUTION_FAILED = 0,
    DEVICE_RUNTIME_COMMAND_EXECUTION_COMPLETED,
} device_runtime_command_execution_t;

static bool device_runtime_ready;

// The factory-reset operation erases the NVS partition, so its guard cannot
// live there. RTC memory survives a software reset long enough to retry a
// failed ACK; a full power loss also removes the cloud session and binding,
// which prevents the same command from being polled again.
#if DEVICE_RUNTIME_HAS_FACTORY_RESET
static RTC_NOINIT_ATTR uint32_t
    device_runtime_factory_reset_completed_magic;
static RTC_NOINIT_ATTR char device_runtime_factory_reset_completed_command_id[
    DEVICE_RUNTIME_COMMAND_ID_SIZE
];
#endif

static void *device_runtime_allocate_buffer(size_t size) {
    if (size == 0) {
        return NULL;
    }
    return heap_caps_malloc_prefer(
        size,
        2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_8BIT
    );
}

#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
static const char *device_runtime_session_state(void) {
    switch (cloud_auth_get_state()) {
        case CLOUD_AUTH_STATE_READY:
            return "ready";
        case CLOUD_AUTH_STATE_REVOKED:
            return "revoked";
        case CLOUD_AUTH_STATE_REAUTH_REQUIRED:
        case CLOUD_AUTH_STATE_AUTHENTICATING:
        case CLOUD_AUTH_STATE_WAITING_FOR_TIME:
        case CLOUD_AUTH_STATE_WAITING_FOR_NETWORK:
        case CLOUD_AUTH_STATE_IDLE:
        default:
            return "reauth_required";
    }
}

static const char *device_runtime_provisioning_state(bool wifi_configured) {
    if (device_binding_client_is_bound()) {
        return "provisioned";
    }
    // Wi-Fi credentials alone do not finish provisioning; the guardian still
    // has to bind the device, so the device stays in the provisioning state.
    if (wifi_configured) {
        return "provisioning";
    }
#if DEVICE_RUNTIME_HAS_DEVICE_PROVISIONING
    if (device_provisioning_is_active()) {
        return "provisioning";
    }
#endif
    return "unprovisioned";
}
#endif

static void device_runtime_mark_platform_rejection(int status_code) {
    if (status_code == 403) {
        cloud_auth_mark_revoked();
        return;
    }
    if (status_code == 401) {
        cloud_auth_mark_unauthorized();
    }
}

static void device_runtime_clear_and_free(
    void *buffer,
    size_t size
) {
    if (buffer == NULL) {
        return;
    }
    if (size > 0) {
        memset(buffer, 0, size);
    }
    free(buffer);
}

static esp_err_t device_runtime_response_buffer_init(
    device_runtime_response_buffer_t *buffer,
    size_t capacity
) {
    if (buffer == NULL || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->body = (char *)device_runtime_allocate_buffer(capacity);
    if (buffer->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    buffer->body_capacity = capacity;
    buffer->body[0] = '\0';
    return ESP_OK;
}

static void device_runtime_response_buffer_destroy(
    device_runtime_response_buffer_t *buffer
) {
    if (buffer == NULL) {
        return;
    }
    device_runtime_clear_and_free(buffer->body, buffer->body_capacity);
    memset(buffer, 0, sizeof(*buffer));
}

static size_t device_runtime_response_buffer_remaining(
    const device_runtime_response_buffer_t *buffer
) {
    if (buffer == NULL || buffer->body == NULL ||
        buffer->body_length >= buffer->body_capacity) {
        return 0;
    }
    return buffer->body_capacity - 1 - buffer->body_length;
}

static bool device_runtime_response_buffer_is_terminated(
    const device_runtime_response_buffer_t *buffer
) {
    return buffer != NULL && buffer->body != NULL &&
        buffer->body_length < buffer->body_capacity &&
        buffer->body[buffer->body_length] == '\0';
}

#if DEVICE_RUNTIME_HAS_FACTORY_RESET
static bool device_runtime_factory_reset_completed_matches(
    const char *command_id
) {
    if (command_id == NULL || command_id[0] == '\0') {
        return false;
    }
    return device_runtime_factory_reset_completed_magic ==
            DEVICE_RUNTIME_FACTORY_RESET_COMPLETED_MAGIC &&
        device_runtime_factory_reset_completed_command_id[
            sizeof(device_runtime_factory_reset_completed_command_id) - 1
        ] == '\0' &&
        strcmp(
            device_runtime_factory_reset_completed_command_id,
            command_id
        ) == 0;
}

static esp_err_t device_runtime_mark_factory_reset_completed(
    const char *command_id
) {
    if (command_id == NULL || command_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    memset(
        device_runtime_factory_reset_completed_command_id,
        0,
        sizeof(device_runtime_factory_reset_completed_command_id)
    );
    snprintf(
        device_runtime_factory_reset_completed_command_id,
        sizeof(device_runtime_factory_reset_completed_command_id),
        "%s",
        command_id
    );
    device_runtime_factory_reset_completed_magic =
        DEVICE_RUNTIME_FACTORY_RESET_COMPLETED_MAGIC;
    return ESP_OK;
}

static void device_runtime_clear_factory_reset_completed(void) {
    device_runtime_factory_reset_completed_magic = 0;
    memset(
        device_runtime_factory_reset_completed_command_id,
        0,
        sizeof(device_runtime_factory_reset_completed_command_id)
    );
}
#endif

static esp_err_t device_runtime_apply_platform_time(
    JsonDocument *response
);

static esp_err_t device_runtime_response_handler(
    esp_http_client_event_t *event
) {
    device_runtime_response_buffer_t *response =
        (device_runtime_response_buffer_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL ||
        event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t remaining =
        device_runtime_response_buffer_remaining(response);
    const size_t copy_length =
        (size_t)event->data_len < remaining
            ? (size_t)event->data_len
            : remaining;
    if (copy_length == 0) {
        response->overflowed = true;
        return ESP_OK;
    }
    if (copy_length < (size_t)event->data_len) {
        response->overflowed = true;
    }
    memcpy(
        response->body + response->body_length,
        event->data,
        copy_length
    );
    response->body_length += copy_length;
    response->body[response->body_length] = '\0';
    return ESP_OK;
}

static esp_err_t device_runtime_platform_request(
    const char *method,
    const char *path,
    const char *request_body,
    const char *session_token,
    JsonDocument *response_document,
    int *status_code_out
) {
    char base_url[DEVICE_RUNTIME_URL_SIZE] = {0};
    esp_err_t result = device_binding_client_get_platform_base_url(
        base_url,
        sizeof(base_url)
    );
    if (result != ESP_OK) {
        return result;
    }
    if (strncmp(base_url, "https://", 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[DEVICE_RUNTIME_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s%s",
        base_url,
        path
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    device_runtime_response_buffer_t response = {};
    result = device_runtime_response_buffer_init(
        &response,
        DEVICE_RUNTIME_RESPONSE_SIZE
    );
    if (result != ESP_OK) {
        return result;
    }
    esp_http_client_config_t client_config = {};
    client_config.url = url;
    client_config.method = HTTP_METHOD_GET;
    client_config.timeout_ms = 15000;
    client_config.event_handler = device_runtime_response_handler;
    client_config.user_data = &response;
    client_config.crt_bundle_attach = esp_crt_bundle_attach;
    if (strcmp(method, "POST") == 0) {
        client_config.method = HTTP_METHOD_POST;
    }
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (client == NULL) {
        device_runtime_response_buffer_destroy(&response);
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (session_token != NULL && session_token[0] != '\0') {
        char authorization[DEVICE_RUNTIME_SESSION_TOKEN_SIZE + 16] = {0};
        const int header_written = snprintf(
            authorization,
            sizeof(authorization),
            "Bearer %s",
            session_token
        );
        if (header_written > 0 &&
            (size_t)header_written < sizeof(authorization)) {
            esp_http_client_set_header(
                client,
                "Authorization",
                authorization
            );
        }
        memset(authorization, 0, sizeof(authorization));
    }
    if (request_body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(
            client,
            request_body,
            (int)strlen(request_body)
        );
    }
    result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (status_code_out != NULL) {
        *status_code_out = status_code;
    }
    if (result != ESP_OK) {
        device_runtime_response_buffer_destroy(&response);
        ESP_LOGW(
            TAG,
            "runtime request failed: %s",
            esp_err_to_name(result)
        );
        return result;
    }
    if (status_code < 200 || status_code >= 300) {
        device_runtime_response_buffer_destroy(&response);
        ESP_LOGW(TAG, "runtime request returned status %d", status_code);
        return status_code == 401 || status_code == 403 ? ESP_ERR_INVALID_STATE
                                  : ESP_ERR_INVALID_RESPONSE;
    }
    if (response_document == NULL) {
        device_runtime_response_buffer_destroy(&response);
        return ESP_OK;
    }
    if (response.overflowed ||
        !device_runtime_response_buffer_is_terminated(&response)) {
        device_runtime_response_buffer_destroy(&response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    JsonDocument envelope;
    if (deserializeJson(envelope, response.body) !=
        DeserializationError::Ok) {
        device_runtime_response_buffer_destroy(&response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    JsonVariant data = envelope["data"];
    if (!data.is<JsonObjectConst>()) {
        device_runtime_response_buffer_destroy(&response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    response_document->set(data);
    device_runtime_response_buffer_destroy(&response);
    return ESP_OK;
}

static void device_runtime_make_heartbeat_id(
    char *output,
    size_t output_size
) {
    const uint32_t random_value = esp_random();
    const TickType_t ticks = xTaskGetTickCount();
    snprintf(
        output,
        output_size,
        "heartbeat_%08lx%08lx",
        (unsigned long)random_value,
        (unsigned long)ticks
    );
}

static void device_runtime_format_time(
    time_t value,
    char *output,
    size_t output_size
) {
    struct tm time_value = {};
    gmtime_r(&value, &time_value);
    strftime(output, output_size, "%Y-%m-%dT%H:%M:%SZ", &time_value);
}

static const char *device_runtime_connection_state(void) {
    switch (network_manager_get_state()) {
        case NETWORK_MANAGER_STATE_CONNECTED:
            return "online";
        case NETWORK_MANAGER_STATE_CONNECTING:
            return "connecting";
        case NETWORK_MANAGER_STATE_IDLE:
        default:
            return "offline";
    }
}

static const char *device_runtime_transport(void) {
    return network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED
               ? "wifi"
               : "none";
}

static const char *device_runtime_time_state(void) {
    switch (time_sync_get_state()) {
        case TIME_SYNC_STATE_SYNCHRONIZED:
            return "synchronized";
        case TIME_SYNC_STATE_SYNCHRONIZING:
            return "synchronizing";
        case TIME_SYNC_STATE_UNSYNCHRONIZED:
        default:
            return "unsynchronized";
    }
}

static const char *device_runtime_time_source(void) {
    switch (time_sync_get_source()) {
        case TIME_SYNC_SOURCE_SNTP:
            return "sntp";
        case TIME_SYNC_SOURCE_PLATFORM:
            return "platform";
        case TIME_SYNC_SOURCE_NONE:
        default:
            return "none";
    }
}

static const char *device_runtime_offline_state(
    offline_fallback_state_t state
) {
    switch (state) {
        case OFFLINE_FALLBACK_STATE_ONLINE:
            return "online";
        case OFFLINE_FALLBACK_STATE_GRACE:
            return "grace";
        case OFFLINE_FALLBACK_STATE_OFFLINE:
        default:
            return "offline";
    }
}

static const char *device_runtime_offline_reason(offline_reason_t reason) {
    switch (reason) {
        case OFFLINE_REASON_NETWORK_UNAVAILABLE:
            return "network_unavailable";
        case OFFLINE_REASON_AUTHENTICATION_FAILED:
            return "authentication_failed";
        case OFFLINE_REASON_TIME_NOT_SYNCHRONIZED:
            return "time_not_synchronized";
        case OFFLINE_REASON_SERVICE_UNAVAILABLE:
            return "service_unavailable";
        case OFFLINE_REASON_NONE:
        default:
            return "none";
    }
}

static esp_err_t device_runtime_send_heartbeat(void) {
    const esp_err_t auth_result = cloud_auth_ensure_authenticated();
    if (auth_result != ESP_OK) {
        offline_fallback_mark_service_unavailable();
        return auth_result;
    }
    char session_token[DEVICE_RUNTIME_SESSION_TOKEN_SIZE] = {0};
    esp_err_t result = device_binding_client_copy_session_token(
        session_token,
        sizeof(session_token)
    );
    if (result != ESP_OK) {
        return result;
    }
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return result;
    }

    char heartbeat_id[DEVICE_RUNTIME_HEARTBEAT_ID_SIZE] = {0};
    device_runtime_make_heartbeat_id(heartbeat_id, sizeof(heartbeat_id));
    char reported_at[32] = {0};
    device_runtime_format_time(time(NULL), reported_at, sizeof(reported_at));

    const network_quality_snapshot_t quality =
        network_quality_get_snapshot();
    const offline_fallback_snapshot_t offline =
        offline_fallback_get_snapshot();
    JsonDocument payload;
    payload["heartbeat_id"] = heartbeat_id;
    payload["reported_at"] = reported_at;
    const esp_app_desc_t *description = esp_app_get_description();
    payload["firmware_version"] =
        description != NULL ? description->version : "0.0.0";
    payload["connection"]["state"] = device_runtime_connection_state();
    payload["connection"]["transport"] = device_runtime_transport();
    payload["network_quality"]["level"] =
        network_quality_level_name(quality.level);
    payload["network_quality"]["rssi_dbm"] = quality.rssi_dbm;
    payload["network_quality"]["latency_ms"] = quality.latency_ms;
    payload["network_quality"]["packet_loss_percent"] =
        quality.packet_loss_percent;
    payload["time_sync"]["state"] = device_runtime_time_state();
    payload["time_sync"]["source"] = device_runtime_time_source();
    payload["time_sync"]["offset_ms"] = time_sync_get_offset_ms();
    if (time_sync_get_last_synced_epoch() > 0) {
        char synced_at[32] = {0};
        device_runtime_format_time(
            (time_t)time_sync_get_last_synced_epoch(),
            synced_at,
            sizeof(synced_at)
        );
        payload["time_sync"]["last_synced_at"] = synced_at;
    } else {
        payload["time_sync"]["last_synced_at"] = nullptr;
    }
    payload["offline"]["state"] =
        device_runtime_offline_state(offline.state);
    payload["offline"]["reason"] =
        device_runtime_offline_reason(offline.reason);
    payload["offline"]["fallback_active"] = offline.fallback_active;
    payload["offline"]["pending_telemetry"] = offline.pending_telemetry;

#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
    provisioning_reporter_snapshot_t *provisioning_events =
        (provisioning_reporter_snapshot_t *)device_runtime_allocate_buffer(
            sizeof(provisioning_reporter_snapshot_t)
        );
    if (provisioning_events == NULL) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    memset(provisioning_events, 0, sizeof(*provisioning_events));
    const esp_err_t provisioning_result =
        provisioning_reporter_get_snapshot(provisioning_events);
    if (provisioning_result == ESP_OK) {
        provisioning_reporter_status_t provisioning_status = {};
        (void)provisioning_reporter_get_status(&provisioning_status);
        JsonObject provisioning_object =
            payload["provisioning"].to<JsonObject>();
        const bool wifi_configured =
            provisioning_status.wifi_configured ||
            network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED;
        provisioning_object["state"] =
            device_runtime_provisioning_state(wifi_configured);
        provisioning_object["wifi_configured"] = wifi_configured;
        provisioning_object["session_state"] =
            device_runtime_session_state();
        if (provisioning_status.last_provisioned_epoch > 0) {
            char provisioned_at[32] = {0};
            device_runtime_format_time(
                (time_t)provisioning_status.last_provisioned_epoch,
                provisioned_at,
                sizeof(provisioned_at)
            );
            provisioning_object["last_provisioned_at"] = provisioned_at;
        } else {
            provisioning_object["last_provisioned_at"] = nullptr;
        }

        JsonArray event_array =
            provisioning_object["events"].to<JsonArray>();
        for (size_t index = 0;
             index < provisioning_events->event_count;
             ++index) {
            const provisioning_reporter_event_t *event =
                &provisioning_events->events[index];
            JsonObject item = event_array.add<JsonObject>();
            item["event_id"] = event->event_id;
            item["event_type"] = event->event_type;
            item["sequence"] = event->sequence;
            item["detail_code"] = event->detail_code;
            item["duration_ms"] = event->duration_ms;
            item["firmware_version"] = event->firmware_version;
        }
    } else if (provisioning_result != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(
            TAG,
            "provisioning snapshot unavailable: %s",
            esp_err_to_name(provisioning_result)
        );
    }
#endif

#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
    diagnostic_reporter_snapshot_t *diagnostics =
        (diagnostic_reporter_snapshot_t *)device_runtime_allocate_buffer(
            sizeof(diagnostic_reporter_snapshot_t)
        );
    if (diagnostics == NULL) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    memset(diagnostics, 0, sizeof(*diagnostics));
    const esp_err_t diagnostics_result =
        diagnostic_reporter_get_snapshot(diagnostics);
    if (diagnostics_result == ESP_OK) {
        JsonObject diagnostics_object =
            payload["diagnostics"].to<JsonObject>();
        diagnostics_object["schema_version"] = "1.0.0";
        diagnostics_object["newest_sequence"] =
            diagnostics->newest_sequence;
        diagnostics_object["dropped_boot_events"] =
            diagnostics->dropped_boot_events;

        JsonArray boot_events =
            diagnostics_object["boot_events"].to<JsonArray>();
        for (size_t index = 0;
             index < diagnostics->boot_event_count;
             ++index) {
            const diagnostic_reporter_boot_event_t *event =
                &diagnostics->boot_events[index];
            JsonObject item = boot_events.add<JsonObject>();
            item["event_id"] = event->event_id;
            item["event_type"] = "boot";
            item["sequence"] = event->sequence;
            item["uptime_ms"] = event->uptime_ms;
            item["boot_count"] = event->boot_count;
            item["reset_reason"] = event->reset_reason;
            item["firmware_version"] = event->firmware_version;
        }

        if (diagnostics->has_failure) {
            JsonObject failure = diagnostics_object["latest_failure"]
                                     .to<JsonObject>();
            char failure_event_id[DIAGNOSTIC_REPORTER_EVENT_ID_SIZE] = {0};
            snprintf(
                failure_event_id,
                sizeof(failure_event_id),
                "failure_%08lx",
                (unsigned long)diagnostics->failure.sequence
            );
            failure["event_id"] = failure_event_id;
            failure["event_type"] = "module_failure";
            failure["sequence"] = diagnostics->failure.sequence;
            failure["module_name"] = diagnostics->failure.module_name;
            failure["error_code"] = diagnostics->failure.error_code;
            failure["failure_count"] = diagnostics->failure.failure_count;
            failure["firmware_version"] =
                diagnostics->failure.firmware_version;
            memset(failure_event_id, 0, sizeof(failure_event_id));
        }

        JsonArray recovery_events =
            diagnostics_object["recovery_events"].to<JsonArray>();
        for (size_t index = 0;
             index < diagnostics->recovery_event_count;
             ++index) {
            const diagnostic_reporter_recovery_event_t *event =
                &diagnostics->recovery_events[index];
            JsonObject item = recovery_events.add<JsonObject>();
            item["event_id"] = event->event_id;
            item["event_type"] = "module_recovered";
            item["sequence"] = event->sequence;
            item["module_name"] = event->module_name;
            item["firmware_version"] = event->firmware_version;
        }
        JsonArray interaction_events =
            diagnostics_object["interaction_events"].to<JsonArray>();
        for (size_t index = 0;
             index < diagnostics->interaction_event_count;
             ++index) {
            const diagnostic_reporter_interaction_event_t *event =
                &diagnostics->interaction_events[index];
            JsonObject item = interaction_events.add<JsonObject>();
            item["event_id"] = event->event_id;
            item["event_type"] = event->event_type;
            item["sequence"] = event->sequence;
            item["detail_code"] = event->detail_code;
            item["duration_ms"] = event->duration_ms;
            item["firmware_version"] = event->firmware_version;
        }
    } else if (diagnostics_result != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(
            TAG,
            "diagnostic snapshot unavailable: %s",
            esp_err_to_name(diagnostics_result)
        );
    }
#endif

    char *request_body = (char *)device_runtime_allocate_buffer(
        DEVICE_RUNTIME_REQUEST_SIZE
    );
    if (request_body == NULL) {
#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
        device_runtime_clear_and_free(
            provisioning_events,
            sizeof(*provisioning_events)
        );
#endif
#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
        device_runtime_clear_and_free(diagnostics, sizeof(*diagnostics));
#endif
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    const size_t request_length = serializeJson(
        payload,
        request_body,
        DEVICE_RUNTIME_REQUEST_SIZE
    );
    if (request_length == 0 ||
        request_length >= DEVICE_RUNTIME_REQUEST_SIZE) {
#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
        device_runtime_clear_and_free(
            provisioning_events,
            sizeof(*provisioning_events)
        );
#endif
#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
        device_runtime_clear_and_free(diagnostics, sizeof(*diagnostics));
#endif
        device_runtime_clear_and_free(
            request_body,
            DEVICE_RUNTIME_REQUEST_SIZE
        );
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_SIZE;
    }
    char path[DEVICE_RUNTIME_URL_SIZE] = {0};
    const int path_written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/runtime/heartbeat",
        device_id
    );
    if (path_written <= 0 || (size_t)path_written >= sizeof(path)) {
#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
        device_runtime_clear_and_free(
            provisioning_events,
            sizeof(*provisioning_events)
        );
#endif
#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
        device_runtime_clear_and_free(diagnostics, sizeof(*diagnostics));
#endif
        device_runtime_clear_and_free(
            request_body,
            DEVICE_RUNTIME_REQUEST_SIZE
        );
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_SIZE;
    }
    int status_code = 0;
    JsonDocument response;
    result = device_runtime_platform_request(
        "POST",
        path,
        request_body,
        session_token,
        &response,
        &status_code
    );
    device_runtime_clear_and_free(
        request_body,
        DEVICE_RUNTIME_REQUEST_SIZE
    );
    memset(session_token, 0, sizeof(session_token));
    if (status_code == 401) {
#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
        device_runtime_clear_and_free(
            provisioning_events,
            sizeof(*provisioning_events)
        );
#endif
#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
        device_runtime_clear_and_free(diagnostics, sizeof(*diagnostics));
#endif
        device_runtime_mark_platform_rejection(status_code);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 403) {
#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
        device_runtime_clear_and_free(
            provisioning_events,
            sizeof(*provisioning_events)
        );
#endif
#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
        device_runtime_clear_and_free(diagnostics, sizeof(*diagnostics));
#endif
        device_runtime_mark_platform_rejection(status_code);
        return ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
        const esp_err_t provisioning_acknowledge_result =
            provisioning_reporter_acknowledge(
                provisioning_events->newest_sequence
            );
        if (provisioning_acknowledge_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "provisioning acknowledgement failed: %s",
                esp_err_to_name(provisioning_acknowledge_result)
            );
        }
        device_runtime_clear_and_free(
            provisioning_events,
            sizeof(*provisioning_events)
        );
        provisioning_events = nullptr;
#endif
#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
        // Acknowledge only after the platform accepted the complete
        // heartbeat. A lost response leaves the same diagnostic records
        // pending for a later heartbeat.
        const esp_err_t acknowledge_result =
            diagnostic_reporter_acknowledge(
                diagnostics->newest_sequence
            );
        if (acknowledge_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "diagnostic acknowledgement failed: %s",
                esp_err_to_name(acknowledge_result)
            );
        }
        device_runtime_clear_and_free(diagnostics, sizeof(*diagnostics));
        diagnostics = nullptr;
#endif
        result = device_runtime_apply_platform_time(&response);
        if (result == ESP_OK) {
            const esp_err_t clear_result =
                offline_fallback_clear_pending_telemetry();
            if (clear_result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "pending telemetry clear failed: %s",
                    esp_err_to_name(clear_result)
                );
            }
        }
    }
#if DEVICE_RUNTIME_HAS_PROVISIONING_REPORTER
    if (result != ESP_OK) {
        device_runtime_clear_and_free(
            provisioning_events,
            sizeof(*provisioning_events)
        );
    }
#endif
#if DEVICE_RUNTIME_HAS_DIAGNOSTIC_REPORTER
    if (result != ESP_OK) {
        device_runtime_clear_and_free(diagnostics, sizeof(*diagnostics));
    }
#endif
    return result;
}

static device_runtime_command_ack_result_t
device_runtime_acknowledge_command(
    const char *device_id,
    const char *command_id,
    const char *status,
    const char *result_code,
    const char *session_token
) {
    JsonDocument payload;
    payload["status"] = status;
    payload["result_code"] = result_code;
    char request_body[DEVICE_RUNTIME_COMMAND_ACK_PAYLOAD_SIZE] = {0};
    const size_t request_length = serializeJson(payload, request_body);
    if (request_length == 0 || request_length >= sizeof(request_body)) {
        return DEVICE_RUNTIME_COMMAND_ACK_INVALID;
    }
    char path[DEVICE_RUNTIME_URL_SIZE] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/runtime/commands/%s/ack",
        device_id,
        command_id
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        return DEVICE_RUNTIME_COMMAND_ACK_INVALID;
    }
    // The platform ignores the path device id and resolves the owner from the
    // device session token, so the command ack uses the same authenticated
    // route without embedding a second identity.
    int status_code = 0;
    const esp_err_t result = device_runtime_platform_request(
        "POST",
        path,
        request_body,
        session_token,
        NULL,
        &status_code
    );
    memset(request_body, 0, sizeof(request_body));
    if (status_code == 401) {
        device_runtime_mark_platform_rejection(status_code);
        return DEVICE_RUNTIME_COMMAND_ACK_UNAUTHORIZED;
    }
    if (status_code == 403) {
        device_runtime_mark_platform_rejection(status_code);
        return DEVICE_RUNTIME_COMMAND_ACK_UNAUTHORIZED;
    }
    if (status_code == 409) {
        // The platform acknowledges a command exactly once. If the success
        // response was lost, the retry receives "already handled"; that is
        // idempotent success and must not hold back a completed reset.
        return DEVICE_RUNTIME_COMMAND_ACK_ALREADY_HANDLED;
    }
    if (result != ESP_OK) {
        return status_code >= 500 || status_code == 0
            ? DEVICE_RUNTIME_COMMAND_ACK_RETRYABLE
            : DEVICE_RUNTIME_COMMAND_ACK_INVALID;
    }
    return DEVICE_RUNTIME_COMMAND_ACK_OK;
}

static device_runtime_command_execution_t device_runtime_execute_command(
    const char *device_id,
    const device_runtime_command_t *command,
    const char *session_token
) {
    const char *result_code = "ok";
    esp_err_t result = ESP_OK;
    if (strcmp(command->type, "reconnect_network") == 0) {
        result = network_manager_reconnect_stored();
    } else if (strcmp(command->type, "resync_time") == 0) {
        result = time_sync_resynchronize();
    } else if (strcmp(command->type, "refresh_configuration") == 0) {
#if DEVICE_RUNTIME_HAS_PARENT_POLICY
        result = parent_policy_refresh();
#else
        result = ESP_OK;
#endif
#if DEVICE_RUNTIME_HAS_FACTORY_RESET
    } else if (strcmp(command->type, "factory_reset") == 0) {
        if (device_runtime_factory_reset_completed_matches(command->id)) {
            // The previous attempt erased device-owned state but did not
            // receive an ACK. Retry only the ACK path; never erase twice.
            result = ESP_OK;
        } else {
            const factory_reset_error_t request_result =
                factory_reset_request(
                    FACTORY_RESET_REASON_GUARDIAN_REQUEST
                );
            if (request_result != FACTORY_RESET_OK) {
                result = ESP_ERR_INVALID_STATE;
                result_code = factory_reset_error_name(request_result);
            } else {
                const factory_reset_error_t confirm_result =
                    factory_reset_confirm();
                result = confirm_result == FACTORY_RESET_OK
                    ? ESP_OK
                    : ESP_ERR_INVALID_STATE;
                if (confirm_result != FACTORY_RESET_OK) {
                    result_code = factory_reset_error_name(confirm_result);
                } else {
                    result = device_runtime_mark_factory_reset_completed(
                        command->id
                    );
                    if (result != ESP_OK) {
                        result_code = "factory_reset_state_error";
                    }
                }
            }
        }
#endif
    } else {
        return DEVICE_RUNTIME_COMMAND_EXECUTION_FAILED;
    }
    const char *status = result == ESP_OK ? "acknowledged" : "failed";
    if (result != ESP_OK && strcmp(result_code, "ok") == 0) {
        result_code = esp_err_to_name(result);
    }
    const device_runtime_command_ack_result_t ack_result =
        device_runtime_acknowledge_command(
            device_id,
            command->id,
            status,
            result_code,
            session_token
        );
    const bool ack_completed =
        ack_result == DEVICE_RUNTIME_COMMAND_ACK_OK ||
        ack_result == DEVICE_RUNTIME_COMMAND_ACK_ALREADY_HANDLED;
#if DEVICE_RUNTIME_HAS_FACTORY_RESET
    if (result == ESP_OK &&
        strcmp(command->type, "factory_reset") == 0) {
        // The acknowledgement must reach the platform before the device
        // restarts; otherwise the operation would be retried indefinitely.
        if (ack_completed) {
            device_runtime_clear_factory_reset_completed();
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
    }
#endif
    if (result != ESP_OK) {
        return DEVICE_RUNTIME_COMMAND_EXECUTION_FAILED;
    }
    return ack_completed
        ? DEVICE_RUNTIME_COMMAND_EXECUTION_COMPLETED
        : DEVICE_RUNTIME_COMMAND_EXECUTION_FAILED;
}

static esp_err_t device_runtime_apply_platform_time(JsonDocument *response) {
    JsonVariant data = response->as<JsonVariant>();
    const char *time_value = data["received_at"];
    if (time_value == NULL) {
        return ESP_OK;
    }
    struct tm parsed_time = {};
    if (strptime(time_value, "%Y-%m-%dT%H:%M:%SZ", &parsed_time) == NULL) {
        return ESP_OK;
    }
    const time_t epoch = timegm(&parsed_time);
    if (epoch <= 0) {
        return ESP_OK;
    }
    return time_sync_accept_platform_time((int64_t)epoch);
}

static esp_err_t device_runtime_poll_commands(const char *session_token) {
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    esp_err_t result = device_identity_copy(device_id, sizeof(device_id));
    if (result != ESP_OK) {
        return result;
    }
    char path[DEVICE_RUNTIME_URL_SIZE] = {0};
    const int written = snprintf(
        path,
        sizeof(path),
        "/api/v1/devices/%s/runtime/commands",
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    JsonDocument response;
    int status_code = 0;
    result = device_runtime_platform_request(
        "GET",
        path,
        NULL,
        session_token,
        &response,
        &status_code
    );
    if (result != ESP_OK) {
        if (status_code == 401 || status_code == 403) {
            device_runtime_mark_platform_rejection(status_code);
        }
        return result;
    }
    JsonArray commands = response["commands"].as<JsonArray>();
    size_t processed = 0;
    for (JsonVariant command_value : commands) {
        if (processed >= DEVICE_RUNTIME_COMMAND_LIMIT) {
            break;
        }
        device_runtime_command_t command = {};
        const char *command_id = command_value["command_id"] | "";
        const char *command_type = command_value["command_type"] | "";
        const char *command_status = command_value["status"] | "";
        const bool is_executable_status =
            strcmp(command_status, "pending") == 0 ||
            strcmp(command_status, "delivered") == 0;
        if (command_id[0] == '\0' || command_type[0] == '\0' ||
            !is_executable_status ||
            strlen(command_id) >= DEVICE_RUNTIME_COMMAND_ID_SIZE ||
            strlen(command_type) >= sizeof(command.type)) {
            continue;
        }
        snprintf(command.id, sizeof(command.id), "%s", command_id);
        snprintf(command.type, sizeof(command.type), "%s", command_type);
        (void)device_runtime_execute_command(
            device_id,
            &command,
            session_token
        );
        ++processed;
    }
    return ESP_OK;
}

static void device_runtime_work_task(void *argument) {
    (void)argument;
    while (true) {
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_bound()) {
            if (cloud_auth_get_state() == CLOUD_AUTH_STATE_REVOKED) {
                offline_fallback_mark_service_unavailable();
                vTaskDelay(pdMS_TO_TICKS(
                    CONFIG_DEVICE_RUNTIME_HEARTBEAT_INTERVAL_SECONDS * 1000
                ));
                continue;
            }
            const esp_err_t heartbeat_result =
                device_runtime_send_heartbeat();
            if (heartbeat_result == ESP_OK) {
                offline_fallback_mark_recovered();
#if DEVICE_RUNTIME_HAS_PARENT_POLICY
                const esp_err_t policy_result =
                    parent_policy_refresh_if_due();
                if (policy_result != ESP_OK &&
                    policy_result != ESP_ERR_NOT_FOUND &&
                    policy_result != ESP_ERR_INVALID_STATE &&
                    policy_result != ESP_ERR_INVALID_VERSION) {
                    offline_fallback_mark_service_unavailable();
                }
#endif
                char session_token[DEVICE_RUNTIME_SESSION_TOKEN_SIZE] = {0};
                if (device_binding_client_copy_session_token(
                        session_token,
                        sizeof(session_token)) == ESP_OK) {
                    device_runtime_poll_commands(session_token);
                }
                memset(session_token, 0, sizeof(session_token));
            } else if (heartbeat_result == ESP_ERR_INVALID_STATE) {
                offline_fallback_mark_service_unavailable();
            }
        } else {
            offline_fallback_record_pending_telemetry();
        }
        vTaskDelay(pdMS_TO_TICKS(
            CONFIG_DEVICE_RUNTIME_HEARTBEAT_INTERVAL_SECONDS * 1000
        ));
    }
}

esp_err_t device_runtime_reporter_init(void) {
    if (device_runtime_ready) {
        return ESP_OK;
    }
    if (!config_store_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(
            device_runtime_work_task,
            "sprout_runtime",
            6144,
            NULL,
            3,
            NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    device_runtime_ready = true;
    return ESP_OK;
}

const module_descriptor_t *device_runtime_reporter_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "device_runtime_reporter",
        .version = "1.0.0",
        .initialize = device_runtime_reporter_init,
        .shutdown = NULL,
    };
    return &descriptor;
}

}  // extern "C"
