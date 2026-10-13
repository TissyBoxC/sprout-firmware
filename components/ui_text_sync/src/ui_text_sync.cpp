#include "ui_text_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "config_store.h"
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
#include "ui_text.h"
#include "ui_text_sync_core.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define UI_TEXT_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define UI_TEXT_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_UI_TEXT_SYNC_INTERVAL_SECONDS
#define CONFIG_UI_TEXT_SYNC_INTERVAL_SECONDS 3600
#endif
#ifndef CONFIG_UI_TEXT_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_UI_TEXT_SYNC_RETRY_INTERVAL_SECONDS 15
#endif
#ifndef CONFIG_UI_TEXT_SYNC_LANGUAGE
#define CONFIG_UI_TEXT_SYNC_LANGUAGE "zh-CN"
#endif

// The revision cursor is small and durable, so it lives beside the other small
// string settings in the default NVS partition while the package blob (tens of
// kilobytes) is routed to the large partition by the config store.
#define UI_TEXT_SYNC_CURSOR_KEY "uitext_rev"
CONFIG_STORE_KEY_ASSERT(UI_TEXT_SYNC_CURSOR_KEY);

// One revision page is bounded by the platform; this caps how many pages a
// single cycle follows so a pathological feed cannot pin the worker forever.
#define UI_TEXT_SYNC_MAX_PAGES 8
#define UI_TEXT_SYNC_PAGE_LIMIT 32

#define UI_TEXT_SYNC_URL_SIZE 384
#define UI_TEXT_SYNC_RESPONSE_SIZE 32768

static const char *const TAG = "ui_text_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} ui_text_sync_response_t;

static esp_err_t ui_text_sync_response_init(
    ui_text_sync_response_t *response
) {
    response->body = (char *)malloc(UI_TEXT_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = UI_TEXT_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void ui_text_sync_response_destroy(
    ui_text_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t ui_text_sync_http_event(esp_http_client_event_t *event) {
    ui_text_sync_response_t *response =
        (ui_text_sync_response_t *)event->user_data;
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

// Resolve the session token, device id, and platform base url shared by every
// request. The caller owns the token and must clear it.
static esp_err_t ui_text_sync_prepare(
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

// Perform one authenticated GET and parse the success envelope. Returns
// ESP_ERR_NOT_FOUND for a 401/403 after clearing the revoked session token so
// the caller can treat it as a terminal authentication state.
static esp_err_t ui_text_sync_get_json(
    const char *url,
    const char *session_token,
    JsonDocument *document
) {
#if UI_TEXT_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif
    ui_text_sync_response_t response = {};
    if (ui_text_sync_response_init(&response) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 20000;
    config.event_handler = ui_text_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ui_text_sync_response_destroy(&response);
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

    if (transport_result != ESP_OK) {
        ui_text_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        ui_text_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_NOT_FOUND;
    }
    if (status_code < 200 || status_code >= 300) {
        ui_text_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        ui_text_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    ui_text_sync_response_destroy(&response);
    return ESP_OK;
}

// Copy one JSON string into a fixed-size destination, returning false when the
// field is missing, empty, or too long to hold safely.
static bool ui_text_sync_copy_field(
    char *destination,
    size_t destination_size,
    const char *value
) {
    if (destination_size == 0) {
        return false;
    }
    destination[0] = '\0';
    if (value == NULL) {
        return false;
    }
    const size_t length = strlen(value);
    if (length == 0 || length >= destination_size) {
        return false;
    }
    memcpy(destination, value, length + 1);
    return true;
}

static int64_t ui_text_sync_revision_cursor(void) {
    char value[32] = {0};
    if (config_store_get_string(
            UI_TEXT_SYNC_CURSOR_KEY,
            value,
            sizeof(value)
        ) != ESP_OK) {
        return 0;
    }
    char *end = NULL;
    const long long parsed = strtoll(value, &end, 10);
    if (end == value || parsed < 0) {
        return 0;
    }
    return (int64_t)parsed;
}

static void ui_text_sync_store_cursor(int64_t revision) {
    char value[32] = {0};
    snprintf(value, sizeof(value), "%lld", (long long)revision);
    const esp_err_t result = config_store_set_string(
        UI_TEXT_SYNC_CURSOR_KEY,
        value
    );
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "cursor persistence failed: %s", esp_err_to_name(result));
    }
}

// Fetch one published package's entries and apply it. A package the device
// already holds at the same or a newer version is skipped so a refresh does
// not rewrite NVS. Any fetch or validation failure leaves the active package
// untouched.
static esp_err_t ui_text_sync_apply_candidate(
    const ui_text_sync_candidate_t *candidate,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    ui_text_package_t active = {};
    if (ui_text_active_package(&active) == ESP_OK &&
        strcmp(active.package_id, candidate->package_id) == 0 &&
        active.package_version >= candidate->package_version) {
        return ESP_OK;
    }

    char url[UI_TEXT_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/ui-text/packages/%s/%s/device_firmware",
        base_url,
        device_id,
        candidate->package_id,
        candidate->language
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }

    JsonDocument document;
    const esp_err_t fetch_result = ui_text_sync_get_json(
        url,
        session_token,
        &document
    );
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }

    JsonObjectConst package = document["data"]["package"].as<JsonObjectConst>();
    JsonArrayConst entries = document["data"]["entries"].as<JsonArrayConst>();
    if (package.isNull() || entries.isNull() || entries.size() == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ui_text_package_t applied = {};
    if (!ui_text_sync_copy_field(
            applied.package_id,
            sizeof(applied.package_id),
            package["package_id"] | ""
        ) ||
        (applied.package_version = package["package_version"] | (uint32_t)0) ==
            0) {
        return ESP_ERR_INVALID_ARG;
    }
    ui_text_sync_copy_field(
        applied.language,
        sizeof(applied.language),
        package["language"] | ""
    );

    const size_t capacity = entries.size();
    ui_text_entry_t *parsed =
        (ui_text_entry_t *)calloc(capacity, sizeof(ui_text_entry_t));
    if (parsed == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t entry_count = 0;
    bool valid = true;
    for (JsonVariantConst value : entries) {
        ui_text_entry_t *slot = &parsed[entry_count];
        if (!ui_text_sync_copy_field(
                slot->ui_text_key,
                sizeof(slot->ui_text_key),
                value["ui_text_key"] | ""
            ) ||
            !ui_text_sync_copy_field(
                slot->value,
                sizeof(slot->value),
                value["value"] | ""
            )) {
            valid = false;
            break;
        }
        const int max_length = value["max_length"] | 0;
        if (max_length < 1 || max_length > UINT16_MAX) {
            valid = false;
            break;
        }
        slot->max_length = (uint16_t)max_length;
        JsonArrayConst variables = value["variables"].as<JsonArrayConst>();
        if (!variables.isNull()) {
            for (JsonVariantConst variable : variables) {
                if (slot->variable_count >= UI_TEXT_MAX_VARIABLES) {
                    break;
                }
                const char *name = variable.as<const char *>();
                if (name == NULL || name[0] == '\0' ||
                    strlen(name) >= UI_TEXT_VARIABLE_SIZE) {
                    continue;
                }
                strncpy(
                    slot->variables[slot->variable_count],
                    name,
                    UI_TEXT_VARIABLE_SIZE - 1
                );
                slot->variable_count += 1;
            }
        }
        entry_count += 1;
    }

    esp_err_t result = ESP_OK;
    if (!valid || entry_count == 0) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        result = ui_text_apply_package(&applied, parsed, entry_count);
    }
    free(parsed);
    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "ui text package %s rejected: %s",
            candidate->package_id,
            esp_err_to_name(result)
        );
    }
    return result;
}

static esp_err_t ui_text_sync_withdraw(
    const char *package_id,
    const char *language
) {
    ui_text_package_t active = {};
    if (ui_text_active_package(&active) != ESP_OK) {
        return ESP_OK;
    }
    if (strcmp(active.package_id, package_id) != 0 ||
        strcmp(active.language, language) != 0) {
        return ESP_OK;
    }
    const esp_err_t result = ui_text_withdraw_package(package_id);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "ui text withdraw failed: %s", esp_err_to_name(result));
    }
    return result;
}

// Fetch and apply one revision page. Returns ESP_OK once the page and every
// action it implies are complete, so the caller only advances the cursor after
// a fully successful page.
static esp_err_t ui_text_sync_page(
    int64_t since_revision,
    const char *session_token,
    const char *device_id,
    const char *base_url,
    int64_t *next_cursor_out,
    bool *has_more_out
) {
    char url[UI_TEXT_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/ui-text/packages?since_revision=%lld&limit=%d",
        base_url,
        device_id,
        (long long)since_revision,
        UI_TEXT_SYNC_PAGE_LIMIT
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    const esp_err_t fetch_result = ui_text_sync_get_json(
        url,
        session_token,
        &document
    );
    if (fetch_result != ESP_OK) {
        return fetch_result;
    }

    JsonObjectConst data = document["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t revision = data["revision"] | (int64_t)0;
    const bool has_more = data["has_more"] | false;

    ui_text_sync_candidate_t candidates[UI_TEXT_SYNC_PAGE_LIMIT] = {};
    size_t candidate_count = 0;
    for (JsonVariantConst value : data["packages"].as<JsonArrayConst>()) {
        if (candidate_count >= UI_TEXT_SYNC_PAGE_LIMIT) {
            break;
        }
        ui_text_sync_candidate_t *candidate = &candidates[candidate_count];
        if (!ui_text_sync_copy_field(
                candidate->package_id,
                sizeof(candidate->package_id),
                value["package_id"] | ""
            ) ||
            !ui_text_sync_copy_field(
                candidate->language,
                sizeof(candidate->language),
                value["language"] | ""
            )) {
            continue;
        }
        candidate->package_version = value["package_version"] | (uint32_t)0;
        candidate->revision = value["revision"] | (int64_t)0;
        candidate_count += 1;
    }

    ui_text_sync_withdrawal_t withdrawals[UI_TEXT_SYNC_PAGE_LIMIT] = {};
    size_t withdrawal_count = 0;
    for (JsonVariantConst value :
         data["withdrawn_packages"].as<JsonArrayConst>()) {
        if (withdrawal_count >= UI_TEXT_SYNC_PAGE_LIMIT) {
            break;
        }
        ui_text_sync_withdrawal_t *withdrawal = &withdrawals[withdrawal_count];
        if (!ui_text_sync_copy_field(
                withdrawal->package_id,
                sizeof(withdrawal->package_id),
                value["package_id"] | ""
            ) ||
            !ui_text_sync_copy_field(
                withdrawal->language,
                sizeof(withdrawal->language),
                value["language"] | ""
            )) {
            continue;
        }
        withdrawal->revision = value["revision"] | (int64_t)0;
        withdrawal_count += 1;
    }

    ui_text_package_t active = {};
    (void)ui_text_active_package(&active);
    ui_text_sync_candidate_t selected = {};
    const ui_text_sync_plan_t plan = ui_text_sync_core_plan(
        candidates,
        candidate_count,
        withdrawals,
        withdrawal_count,
        CONFIG_UI_TEXT_SYNC_LANGUAGE,
        active.package_id,
        &selected
    );
    esp_err_t result = ESP_OK;
    if (plan == UI_TEXT_SYNC_PLAN_APPLY) {
        result = ui_text_sync_apply_candidate(
            &selected,
            session_token,
            device_id,
            base_url
        );
    } else if (plan == UI_TEXT_SYNC_PLAN_WITHDRAW) {
        result = ui_text_sync_withdraw(
            active.package_id,
            CONFIG_UI_TEXT_SYNC_LANGUAGE
        );
    }
    if (result != ESP_OK) {
        return result;
    }

    *has_more_out = has_more;
    if (has_more) {
        const char *cursor = data["next_cursor"] | "";
        char *end = NULL;
        const long long parsed = strtoll(cursor, &end, 10);
        *next_cursor_out = (end != cursor && parsed > since_revision)
            ? (int64_t)parsed
            : revision;
    } else {
        *next_cursor_out = revision;
    }
    return ESP_OK;
}

esp_err_t ui_text_sync_now(void) {
    if (!ui_text_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[UI_TEXT_SYNC_URL_SIZE] = {0};
    if (ui_text_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t cursor = ui_text_sync_revision_cursor();
    esp_err_t result = ESP_OK;
    for (int page = 0; page < UI_TEXT_SYNC_MAX_PAGES; ++page) {
        int64_t next_cursor = cursor;
        bool has_more = false;
        result = ui_text_sync_page(
            cursor,
            session_token,
            device_id,
            base_url,
            &next_cursor,
            &has_more
        );
        if (result != ESP_OK) {
            break;
        }
        // Only advance the durable cursor after a page fully completes, so a
        // transient failure re-delivers the same page next cycle.
        cursor = next_cursor;
        ui_text_sync_store_cursor(cursor);
        if (!has_more) {
            break;
        }
    }
    memset(session_token, 0, sizeof(session_token));
    return result;
}

static void ui_text_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = ui_text_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "ui text sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_UI_TEXT_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_UI_TEXT_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t ui_text_sync_initialize(void) {
    if (!ui_text_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            ui_text_sync_worker,
            "ui_text_sync",
            8192,
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

const module_descriptor_t *ui_text_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "ui_text_sync",
        .version = "1.0.0",
        .initialize = ui_text_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
