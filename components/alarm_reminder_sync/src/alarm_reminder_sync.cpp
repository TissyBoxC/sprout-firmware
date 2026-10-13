#include "alarm_reminder_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "alarm_reminder.h"
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
#define ALARM_REMINDER_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define ALARM_REMINDER_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_ALARM_REMINDER_SYNC_INTERVAL_SECONDS
#define CONFIG_ALARM_REMINDER_SYNC_INTERVAL_SECONDS 900
#endif
#ifndef CONFIG_ALARM_REMINDER_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_ALARM_REMINDER_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define ALARM_REMINDER_SYNC_URL_SIZE 384
#define ALARM_REMINDER_SYNC_RESPONSE_SIZE 32768
#define ALARM_REMINDER_SYNC_REQUEST_SIZE 256

static const char *const TAG = "alarm_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} alarm_reminder_sync_response_t;

static esp_err_t alarm_reminder_sync_response_init(
    alarm_reminder_sync_response_t *response
) {
    response->body = (char *)malloc(ALARM_REMINDER_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = ALARM_REMINDER_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void alarm_reminder_sync_response_destroy(
    alarm_reminder_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t alarm_reminder_sync_http_event(
    esp_http_client_event_t *event
) {
    alarm_reminder_sync_response_t *response =
        (alarm_reminder_sync_response_t *)event->user_data;
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

// Resolve the session token, device id and platform base url shared by the
// schedule fetch and the firing-event reports. The caller owns the token.
static esp_err_t alarm_reminder_sync_prepare(
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

static esp_err_t alarm_reminder_sync_fetch(JsonDocument *document) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[ALARM_REMINDER_SYNC_URL_SIZE] = {0};
    if (alarm_reminder_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char url[ALARM_REMINDER_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/alarm-reminders",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if ALARM_REMINDER_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    alarm_reminder_sync_response_t response = {};
    if (alarm_reminder_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.event_handler = alarm_reminder_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        alarm_reminder_sync_response_destroy(&response);
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
        alarm_reminder_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        alarm_reminder_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        alarm_reminder_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        alarm_reminder_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    alarm_reminder_sync_response_destroy(&response);
    return ESP_OK;
}

// Copy one JSON string into a fixed-size destination, keeping the field empty
// when the value is missing or too long to hold safely.
static void alarm_reminder_sync_copy_field(
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

// Parse a 24-hour HH:MM wall-clock string into minutes past midnight. Returns
// false when the value is missing or out of range.
static bool alarm_reminder_sync_parse_time(
    const char *value,
    uint16_t *minute_of_day_out
) {
    if (value == NULL || strlen(value) != 5 || value[2] != ':') {
        return false;
    }
    const int hour = (value[0] - '0') * 10 + (value[1] - '0');
    const int minute = (value[3] - '0') * 10 + (value[4] - '0');
    if (value[0] < '0' || value[0] > '9' || value[1] < '0' || value[1] > '9' ||
        value[3] < '0' || value[3] > '9' || value[4] < '0' || value[4] > '9') {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    *minute_of_day_out = (uint16_t)(hour * 60 + minute);
    return true;
}

static esp_err_t alarm_reminder_sync_apply_document(JsonDocument &document) {
    JsonArrayConst alarms = document["data"]["alarms"].as<JsonArrayConst>();
    if (alarms.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    alarm_reminder_t parsed[ALARM_REMINDER_CAPACITY] = {};
    size_t alarm_count = 0;
    size_t entry_count = 0;
    for (JsonVariantConst value : alarms) {
        entry_count += 1;
        if (alarm_count >= ALARM_REMINDER_CAPACITY) {
            continue;
        }
        alarm_reminder_t alarm = {};
        alarm_reminder_sync_copy_field(
            alarm.alarm_id,
            sizeof(alarm.alarm_id),
            value["id"] | ""
        );
        alarm_reminder_sync_copy_field(
            alarm.child_id,
            sizeof(alarm.child_id),
            value["child_id"] | ""
        );
        alarm_reminder_sync_copy_field(
            alarm.label,
            sizeof(alarm.label),
            value["label"] | ""
        );
        alarm_reminder_sync_copy_field(
            alarm.prompt,
            sizeof(alarm.prompt),
            value["prompt"] | ""
        );
        if (alarm.alarm_id[0] == '\0' || alarm.label[0] == '\0' ||
            !alarm_reminder_sync_parse_time(
                value["alarm_time"] | "",
                &alarm.minute_of_day
            )) {
            continue;
        }
        uint8_t weekday_mask = 0;
        JsonArrayConst weekdays = value["weekdays"].as<JsonArrayConst>();
        if (!weekdays.isNull()) {
            for (JsonVariantConst day : weekdays) {
                const int weekday = day | 0;
                if (weekday < 1 || weekday > ALARM_REMINDER_WEEKDAY_COUNT) {
                    continue;
                }
                weekday_mask |= (uint8_t)(1u << (weekday - 1));
            }
        }
        // The platform expands an empty weekday set to "every day", but keep a
        // local fallback so a stripped-down payload still rings every day.
        alarm.weekday_mask = weekday_mask != 0 ? weekday_mask : 0x7Fu;
        const int snooze = value["snooze_minutes"] | 5;
        alarm.snooze_minutes = (uint8_t)(
            snooze < ALARM_REMINDER_MIN_SNOOZE_MINUTES
                ? ALARM_REMINDER_MIN_SNOOZE_MINUTES
                : (snooze > ALARM_REMINDER_MAX_SNOOZE_MINUTES
                       ? ALARM_REMINDER_MAX_SNOOZE_MINUTES
                       : snooze)
        );
        // The device schedule only carries enabled alarms.
        alarm.enabled = true;
        alarm.state = ALARM_REMINDER_STATE_IDLE;
        parsed[alarm_count] = alarm;
        alarm_count += 1;
    }
    if (alarm_count == 0 && entry_count > 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return alarm_reminder_replace_schedule(parsed, alarm_count);
}

// Format one YYYYMMDD local date as the platform's YYYY-MM-DD. Returns false
// for a malformed date so the event is left queued for a later retry.
static bool alarm_reminder_sync_format_date(
    int32_t alarm_date,
    char *destination,
    size_t destination_size
) {
    if (destination_size < 11 || alarm_date < 10000101) {
        return false;
    }
    const int year = (int)(alarm_date / 10000);
    const int month = (int)((alarm_date / 100) % 100);
    const int day = (int)(alarm_date % 100);
    const int written = snprintf(
        destination,
        destination_size,
        "%04d-%02d-%02d",
        year,
        month,
        day
    );
    return written == 10;
}

// Report one firing event to the platform. Returns ESP_OK once the platform
// accepts it so the caller can drain the event from the queue. A 404 means the
// alarm no longer exists (the guardian removed it) and is reported as
// ESP_ERR_NOT_FOUND so the caller can discard the stale event instead of
// retrying it forever; any other result keeps the event queued.
static esp_err_t alarm_reminder_sync_report_event(
    const alarm_reminder_pending_event_t *event,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    char alarm_date[11] = {0};
    if (!alarm_reminder_sync_format_date(
            event->alarm_date,
            alarm_date,
            sizeof(alarm_date)
        )) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[ALARM_REMINDER_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/alarm-reminders/%s/events",
        base_url,
        device_id,
        event->alarm_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
#if ALARM_REMINDER_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif

    JsonDocument payload;
    payload["alarm_date"] = alarm_date;
    payload["state"] = alarm_reminder_state_name(event->state);
    char request_body[ALARM_REMINDER_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length =
        serializeJson(payload, request_body, sizeof(request_body));
    if (body_length == 0 || body_length >= sizeof(request_body)) {
        return ESP_ERR_INVALID_ARG;
    }

    alarm_reminder_sync_response_t response = {};
    if (alarm_reminder_sync_response_init(&response) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.event_handler = alarm_reminder_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        alarm_reminder_sync_response_destroy(&response);
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
    alarm_reminder_sync_response_destroy(&response);
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

// Drain the queued firing events, stopping at the first failure so a transient
// error leaves the rest of the queue intact for the next cycle.
static void alarm_reminder_sync_report_events(void) {
    if (alarm_reminder_pending_event_count() == 0) {
        return;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[ALARM_REMINDER_SYNC_URL_SIZE] = {0};
    if (alarm_reminder_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return;
    }
    alarm_reminder_pending_event_t events[
        ALARM_REMINDER_PENDING_EVENT_CAPACITY
    ] = {};
    size_t event_count = 0;
    if (alarm_reminder_list_pending_events(
            events,
            ALARM_REMINDER_PENDING_EVENT_CAPACITY,
            &event_count
        ) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }
    for (size_t index = 0; index < event_count; ++index) {
        const esp_err_t report_result = alarm_reminder_sync_report_event(
            &events[index],
            session_token,
            device_id,
            base_url
        );
        if (report_result == ESP_OK || report_result == ESP_ERR_NOT_FOUND) {
            // A stale event for a removed alarm is dropped rather than retried
            // forever.
            (void)alarm_reminder_ack_event(
                events[index].alarm_id,
                events[index].alarm_date
            );
        } else {
            ESP_LOGW(
                TAG,
                "alarm event report failed for %s: %s",
                events[index].alarm_id,
                esp_err_to_name(report_result)
            );
            break;
        }
    }
    memset(session_token, 0, sizeof(session_token));
}

esp_err_t alarm_reminder_sync_now(void) {
    if (!alarm_reminder_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    JsonDocument document;
    const esp_err_t fetch_result = alarm_reminder_sync_fetch(&document);
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }
    const esp_err_t apply_result =
        alarm_reminder_sync_apply_document(document);
    if (apply_result != ESP_OK) {
        ESP_LOGW(TAG, "alarm schedule response rejected: %s",
                 esp_err_to_name(apply_result));
        return apply_result;
    }
    alarm_reminder_sync_report_events();
    return ESP_OK;
}

static void alarm_reminder_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = alarm_reminder_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "alarm schedule sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_ALARM_REMINDER_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_ALARM_REMINDER_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t alarm_reminder_sync_initialize(void) {
    if (!alarm_reminder_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            alarm_reminder_sync_worker,
            "alarm_sync",
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

const module_descriptor_t *alarm_reminder_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "alarm_reminder_sync",
        .version = "1.0.0",
        .initialize = alarm_reminder_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
