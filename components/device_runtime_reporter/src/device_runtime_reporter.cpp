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
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "network_quality.h"
#include "offline_fallback.h"
#include "time_sync.h"

#define DEVICE_RUNTIME_RESPONSE_SIZE 8192
#define DEVICE_RUNTIME_URL_SIZE 320
#define DEVICE_RUNTIME_SESSION_TOKEN_SIZE DEVICE_BINDING_SESSION_TOKEN_SIZE
#define DEVICE_RUNTIME_HEARTBEAT_ID_SIZE 64
#define DEVICE_RUNTIME_COMMAND_ID_SIZE 64
#define DEVICE_RUNTIME_COMMAND_LIMIT 4

static const char *const TAG = "device_runtime";

typedef struct {
    char body[DEVICE_RUNTIME_RESPONSE_SIZE];
    size_t body_length;
} device_runtime_response_t;

typedef struct {
    char id[DEVICE_RUNTIME_COMMAND_ID_SIZE];
    char type[32];
} device_runtime_command_t;

static bool device_runtime_ready;

static esp_err_t device_runtime_apply_platform_time(
    JsonDocument *response
);

static esp_err_t device_runtime_response_handler(
    esp_http_client_event_t *event
) {
    device_runtime_response_t *response =
        (device_runtime_response_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL ||
        event->data_len <= 0) {
        return ESP_OK;
    }
    const size_t remaining =
        sizeof(response->body) - 1 - response->body_length;
    const size_t copy_length =
        (size_t)event->data_len < remaining
            ? (size_t)event->data_len
            : remaining;
    if (copy_length == 0) {
        return ESP_OK;
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
    esp_err_t result = config_store_get_string(
        "platform_base_url",
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

    device_runtime_response_t response{};
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
        ESP_LOGW(
            TAG,
            "runtime request failed: %s",
            esp_err_to_name(result)
        );
        return result;
    }
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "runtime request returned status %d", status_code);
        return status_code == 401 ? ESP_ERR_INVALID_STATE
                                  : ESP_ERR_INVALID_RESPONSE;
    }
    if (response_document == NULL) {
        return ESP_OK;
    }
    JsonDocument envelope;
    if (deserializeJson(envelope, response.body) !=
        DeserializationError::Ok) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    JsonVariant data = envelope["data"];
    if (!data.is<JsonObjectConst>()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    response_document->set(data);
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

    char request_body[DEVICE_RUNTIME_RESPONSE_SIZE] = {0};
    const size_t request_length = serializeJson(payload, request_body);
    if (request_length == 0 || request_length >= sizeof(request_body)) {
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
    memset(request_body, 0, sizeof(request_body));
    memset(session_token, 0, sizeof(session_token));
    if (status_code == 401) {
        cloud_auth_mark_unauthorized();
        return ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        result = device_runtime_apply_platform_time(&response);
    }
    return result;
}

static esp_err_t device_runtime_acknowledge_command(
    const char *device_id,
    const char *command_id,
    const char *status,
    const char *result_code,
    const char *session_token
) {
    JsonDocument payload;
    payload["status"] = status;
    payload["result_code"] = result_code;
    char request_body[DEVICE_RUNTIME_RESPONSE_SIZE] = {0};
    const size_t request_length = serializeJson(payload, request_body);
    if (request_length == 0 || request_length >= sizeof(request_body)) {
        return ESP_ERR_INVALID_SIZE;
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
        return ESP_ERR_INVALID_SIZE;
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
    return status_code == 401 ? ESP_ERR_INVALID_STATE : result;
}

static esp_err_t device_runtime_execute_command(
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
        result = ESP_OK;
    } else {
        result = ESP_ERR_INVALID_ARG;
    }
    const char *status = result == ESP_OK ? "acknowledged" : "failed";
    if (result != ESP_OK) {
        result_code = esp_err_to_name(result);
    }
    const esp_err_t ack_result = device_runtime_acknowledge_command(
        device_id,
        command->id,
        status,
        result_code,
        session_token
    );
    return ack_result == ESP_OK ? result : ack_result;
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
        if (status_code == 401) {
            cloud_auth_mark_unauthorized();
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
        if (command_id[0] == '\0' || command_type[0] == '\0' ||
            strcmp(command_status, "pending") != 0) {
            continue;
        }
        snprintf(command.id, sizeof(command.id), "%s", command_id);
        snprintf(command.type, sizeof(command.type), "%s", command_type);
        device_runtime_execute_command(device_id, &command, session_token);
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
            const esp_err_t heartbeat_result =
                device_runtime_send_heartbeat();
            if (heartbeat_result == ESP_OK) {
                offline_fallback_mark_recovered();
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
