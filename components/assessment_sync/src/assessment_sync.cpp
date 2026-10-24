#include "assessment_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "assessment_sync_core.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "time_sync.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define ASSESSMENT_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define ASSESSMENT_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_ASSESSMENT_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_ASSESSMENT_SYNC_RETRY_INTERVAL_SECONDS 15
#endif
#ifndef CONFIG_ASSESSMENT_SYNC_MAX_RETRY_INTERVAL_SECONDS
#define CONFIG_ASSESSMENT_SYNC_MAX_RETRY_INTERVAL_SECONDS 300
#endif
#ifndef CONFIG_ASSESSMENT_SYNC_POLL_INTERVAL_SECONDS
#define CONFIG_ASSESSMENT_SYNC_POLL_INTERVAL_SECONDS 60
#endif
#ifndef CONFIG_ASSESSMENT_SYNC_TASK_STACK_SIZE
#define CONFIG_ASSESSMENT_SYNC_TASK_STACK_SIZE 6144
#endif

#define ASSESSMENT_SYNC_URL_SIZE 384
#define ASSESSMENT_SYNC_RESPONSE_SIZE 8192
#define ASSESSMENT_SYNC_REQUEST_SIZE 512

// An assessment_start command carries only the session id and a deadline, so a
// generous bound still refuses anything that is not one small JSON object.
#define ASSESSMENT_SYNC_COMMAND_MAX_BYTES 512

static const char *const TAG = "assessment_sync";

static SemaphoreHandle_t s_lock;
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

static assessment_sync_session_t s_session;
static bool s_has_session;
static assessment_sync_answer_t s_answers[ASSESSMENT_SYNC_PENDING_CAPACITY];
static size_t s_answer_count;
static assessment_sync_status_t s_status = ASSESSMENT_SYNC_STATUS_IDLE;
static int64_t s_last_success_epoch_seconds;
static uint32_t s_failure_count;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} assessment_sync_response_t;

static bool assessment_sync_ensure_lock(void) {
    if (s_lock != NULL) {
        return true;
    }
    s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL;
}

static esp_err_t assessment_sync_response_init(
    assessment_sync_response_t *response
) {
    response->body = (char *)malloc(ASSESSMENT_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = ASSESSMENT_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void assessment_sync_response_destroy(
    assessment_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t assessment_sync_http_event(
    esp_http_client_event_t *event
) {
    assessment_sync_response_t *response =
        (assessment_sync_response_t *)event->user_data;
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

// Resolve the session token, device id and platform base url shared by every
// request. The caller owns and must clear the token buffer.
static esp_err_t assessment_sync_prepare(
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

// Issue one device-authenticated request and parse the JSON body on success.
//
// A 401/403 clears the stored session token so the next attempt performs a
// fresh challenge instead of replaying a revoked credential.
static esp_err_t assessment_sync_request(
    const char *url,
    esp_http_client_method_t method,
    const char *session_token,
    const char *request_body,
    JsonDocument *document
) {
#if ASSESSMENT_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif
    assessment_sync_response_t response = {};
    if (assessment_sync_response_init(&response) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = method;
    config.timeout_ms = 15000;
    config.event_handler = assessment_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        assessment_sync_response_destroy(&response);
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
    if (request_body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(
            client,
            request_body,
            (int)strlen(request_body)
        );
    }
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    if (transport_result != ESP_OK) {
        assessment_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        assessment_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 404 || status_code == 409 || status_code == 422) {
        // The session is over, belongs to another device, or the answer is
        // permanently unacceptable. Retrying the same request cannot help.
        assessment_sync_response_destroy(&response);
        return ESP_ERR_NOT_FOUND;
    }
    if (status_code < 200 || status_code >= 300) {
        assessment_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        assessment_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    assessment_sync_response_destroy(&response);
    return ESP_OK;
}

// Apply the `data` object of a session response to the tracked snapshot.
//
// Returns false when the payload cannot be trusted, so the caller keeps what it
// had instead of showing a half-parsed item.
static bool assessment_sync_apply_document(
    JsonVariantConst data,
    assessment_sync_session_t *session_out
) {
    if (!data.is<JsonObjectConst>()) {
        return false;
    }
    const char *session_id = data["session_id"] | "";
    const char *state = data["state"] | "";
    const bool accepts_answers = data["accepts_answers"] | false;
    const int32_t difficulty = data["difficulty"] | 0;
    const int32_t answered_count = data["answered_count"] | 0;
    const int32_t correct_count = data["correct_count"] | 0;
    const int32_t mastery = data["mastery"] | 0;
    JsonVariantConst next_item = data["next_item"];
    const bool has_item = next_item.is<JsonObjectConst>();
    const int32_t item_position = next_item["position"] | -1;
    const char *item_id = next_item["item_id"] | "";
    const char *item_type = next_item["item_type"] | "";
    const char *item_text = next_item["text"] | "";
    const int32_t item_difficulty = next_item["difficulty"] | 0;
    return assessment_sync_parse_session(
        session_out,
        session_id,
        state,
        accepts_answers,
        difficulty,
        answered_count,
        correct_count,
        mastery,
        has_item,
        item_position,
        item_id,
        item_type,
        item_text,
        item_difficulty
    );
}

// Report one queued answer. The body deliberately omits `position`: the
// platform derives the next slot from the stored answer count, so a retried
// request can neither skip nor duplicate an item.
static esp_err_t assessment_sync_report_answer(
    const assessment_sync_answer_t *answer,
    const char *session_id,
    const char *session_token,
    const char *device_id,
    const char *base_url,
    assessment_sync_session_t *session_out
) {
    char url[ASSESSMENT_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/assessment-sessions/%s/answers",
        base_url,
        device_id,
        session_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument payload;
    payload["item_id"] = answer->item_id;
    payload["item_type"] = answer->item_type;
    payload["target_text"] = answer->target_text;
    payload["score"] = answer->score;
    char request_body[ASSESSMENT_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length =
        serializeJson(payload, request_body, sizeof(request_body));
    if (body_length == 0 || body_length >= sizeof(request_body)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    const esp_err_t request_result = assessment_sync_request(
        url,
        HTTP_METHOD_POST,
        session_token,
        request_body,
        &document
    );
    if (request_result != ESP_OK) {
        return request_result;
    }
    // A response the device cannot parse is treated as a successful write: the
    // answer is already stored, and re-sending it would be refused as a
    // duplicate position. The next poll corrects the local snapshot.
    (void)assessment_sync_apply_document(document["data"], session_out);
    return ESP_OK;
}

static esp_err_t assessment_sync_fetch_session(
    const char *session_id,
    const char *session_token,
    const char *device_id,
    const char *base_url,
    assessment_sync_session_t *session_out
) {
    char url[ASSESSMENT_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/assessment-sessions/%s",
        base_url,
        device_id,
        session_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument document;
    const esp_err_t request_result = assessment_sync_request(
        url,
        HTTP_METHOD_GET,
        session_token,
        NULL,
        &document
    );
    if (request_result != ESP_OK) {
        return request_result;
    }
    if (!assessment_sync_apply_document(document["data"], session_out)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// Drain queued answers and refresh the session in one cycle.
//
// The upload runs first so the pull reflects the answers just accepted. The
// caller owns the session id because an earlier refresh may not have produced
// a snapshot for it yet.
static esp_err_t assessment_sync_run(const char *session_id) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[ASSESSMENT_SYNC_URL_SIZE] = {0};
    const esp_err_t prepare_result = assessment_sync_prepare(
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

    assessment_sync_session_t updated;
    memset(&updated, 0, sizeof(updated));
    esp_err_t result = ESP_OK;
    bool session_changed = false;
    for (;;) {
        assessment_sync_answer_t answer;
        memset(&answer, 0, sizeof(answer));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const size_t queued = s_answer_count;
        if (queued > 0) {
            answer = s_answers[0];
        }
        xSemaphoreGive(s_lock);
        if (queued == 0) {
            break;
        }
        result = assessment_sync_report_answer(
            &answer,
            session_id,
            session_token,
            device_id,
            base_url,
            &updated
        );
        if (result == ESP_OK) {
            session_changed = true;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            assessment_sync_answer_pop(s_answers, &s_answer_count);
            xSemaphoreGive(s_lock);
            continue;
        }
        if (result == ESP_ERR_NOT_FOUND) {
            // A permanently refused answer must not block the queue forever,
            // so it is dropped and the drain continues with the next one.
            ESP_LOGW(TAG, "assessment answer refused by the platform");
            xSemaphoreTake(s_lock, portMAX_DELAY);
            assessment_sync_answer_pop(s_answers, &s_answer_count);
            xSemaphoreGive(s_lock);
            continue;
        }
        // A transient failure keeps the answer queued for the next cycle.
        memset(session_token, 0, sizeof(session_token));
        return result;
    }
    const esp_err_t fetch_result = assessment_sync_fetch_session(
        session_id,
        session_token,
        device_id,
        base_url,
        &updated
    );
    if (fetch_result == ESP_OK) {
        session_changed = true;
    }
    if (session_changed) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_session = updated;
        s_has_session = true;
        s_last_success_epoch_seconds = (int64_t)time(NULL);
        xSemaphoreGive(s_lock);
        // Only identifiers and progress are logged. The item text and the
        // score are the child's work, and no log sink may receive them.
        ESP_LOGI(
            TAG,
            "assessment session %s state=%s answered=%ld item=%s",
            session_id,
            updated.state,
            (long)updated.answered_count,
            updated.item.has_item ? updated.item.item_id : "none"
        );
    }
    memset(session_token, 0, sizeof(session_token));
    return fetch_result;
}

esp_err_t assessment_sync_refresh_session(const char *session_id) {
    // The argument is checked before the ready flag so a caller bug is reported
    // as one, whatever state the module is in.
    if (session_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char tracked[ASSESSMENT_SYNC_SESSION_ID_SIZE] = {0};
    const size_t length = strlen(session_id);
    if (length == 0 || length >= sizeof(tracked)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(tracked, session_id, length + 1);
    if (!s_ready || !assessment_sync_ensure_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    // A new session id invalidates the queued answers of the previous run;
    // they can never be reported against this session's positions. Whether the
    // id is new is decided by s_has_session: s_session.session_id may already
    // hold this id from an earlier call that never produced a snapshot.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_has_session && strcmp(s_session.session_id, tracked) != 0) {
        memset(s_answers, 0, sizeof(s_answers));
        s_answer_count = 0;
        s_has_session = false;
    }
    xSemaphoreGive(s_lock);

    const esp_err_t result = assessment_sync_run(tracked);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (result == ESP_OK) {
        s_status = ASSESSMENT_SYNC_STATUS_IDLE;
    } else if (result == ESP_ERR_INVALID_STATE) {
        s_status = ASSESSMENT_SYNC_STATUS_OFFLINE;
    } else {
        s_status = ASSESSMENT_SYNC_STATUS_FAILED;
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t assessment_sync_handle_command(const char *payload, size_t length) {
    if (payload == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    // The reporter hands over the exact bytes it received, so this module owns
    // bounding them. A command is a handful of short fields.
    if (length > ASSESSMENT_SYNC_COMMAND_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    JsonDocument document;
    if (deserializeJson(document, payload, length)) {
        return ESP_ERR_INVALID_ARG;
    }
    JsonObjectConst root = document.as<JsonObjectConst>();
    const char *session_id = root["session_id"];
    if (session_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return assessment_sync_refresh_session(session_id);
}

esp_err_t assessment_sync_submit_answer(
    const char *session_id,
    const char *item_id,
    const char *item_type,
    const char *target_text,
    int score
) {
    assessment_sync_answer_t answer;
    // As with refresh, a malformed answer is a caller bug and is reported as
    // such before the module state is consulted.
    if (session_id == NULL ||
        !assessment_sync_answer_from_values(
            &answer,
            item_id,
            item_type,
            target_text,
            score
        )) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready || !assessment_sync_ensure_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // An answer is only meaningful for the session the device is showing. Any
    // other session id means the run already ended or was replaced.
    if (!s_has_session || strcmp(s_session.session_id, session_id) != 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_session.accepts_answers) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (!assessment_sync_answer_matches_item(
            &s_session.item,
            answer.item_id,
            answer.item_type,
            answer.target_text
        )) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    const bool pushed = assessment_sync_answer_push(
        s_answers,
        ASSESSMENT_SYNC_PENDING_CAPACITY,
        &s_answer_count,
        &answer
    );
    xSemaphoreGive(s_lock);
    return pushed ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t assessment_sync_get_status(
    assessment_sync_status_snapshot_t *status_out
) {
    if (status_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(status_out, 0, sizeof(*status_out));
    status_out->status = s_status;
    if (!s_ready || !assessment_sync_ensure_lock()) {
        return ESP_OK;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    status_out->status = s_status;
    status_out->has_session = s_has_session;
    memcpy(status_out->state, s_session.state, sizeof(status_out->state));
    status_out->accepts_answers = s_session.accepts_answers;
    status_out->difficulty = s_session.difficulty;
    status_out->answered_count = s_session.answered_count;
    status_out->correct_count = s_session.correct_count;
    status_out->mastery = s_session.mastery;
    status_out->item = s_session.item;
    status_out->pending_count = s_answer_count;
    status_out->last_success_epoch_seconds = s_last_success_epoch_seconds;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t assessment_sync_reset(void) {
    if (!s_ready || !assessment_sync_ensure_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(&s_session, 0, sizeof(s_session));
    memset(s_answers, 0, sizeof(s_answers));
    s_answer_count = 0;
    s_has_session = false;
    s_failure_count = 0;
    s_status = ASSESSMENT_SYNC_STATUS_IDLE;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

static void assessment_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool tracked = s_has_session;
        char session_id[ASSESSMENT_SYNC_SESSION_ID_SIZE] = {0};
        memcpy(session_id, s_session.session_id, sizeof(session_id));
        xSemaphoreGive(s_lock);

        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (tracked) {
            result = assessment_sync_refresh_session(session_id);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (result == ESP_OK) {
                s_failure_count = 0;
            } else {
                s_failure_count += 1;
            }
            xSemaphoreGive(s_lock);
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                // The error name alone is logged; nothing about the child's
                // answers belongs in a log sink.
                ESP_LOGW(TAG, "assessment sync failed: %s",
                         esp_err_to_name(result));
            }
        }
        uint32_t delay_seconds = CONFIG_ASSESSMENT_SYNC_POLL_INTERVAL_SECONDS;
        if (result != ESP_OK) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            const uint32_t failures = s_failure_count;
            xSemaphoreGive(s_lock);
            delay_seconds = assessment_sync_backoff_seconds(
                CONFIG_ASSESSMENT_SYNC_RETRY_INTERVAL_SECONDS,
                CONFIG_ASSESSMENT_SYNC_MAX_RETRY_INTERVAL_SECONDS,
                failures
            );
        }
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t assessment_sync_initialize(void) {
    if (s_ready) {
        return ESP_OK;
    }
    if (!assessment_sync_ensure_lock()) {
        return ESP_ERR_NO_MEM;
    }
    s_stop = false;
    if (xTaskCreate(
            assessment_sync_worker,
            "assessment",
            CONFIG_ASSESSMENT_SYNC_TASK_STACK_SIZE,
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

const module_descriptor_t *assessment_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "assessment_sync",
        .version = "1.0.0",
        .initialize = assessment_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef ASSESSMENT_SYNC_HOST_TESTS
extern "C" {

bool assessment_sync_host_is_ready(void) {
    return s_ready;
}

void assessment_sync_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
    s_failure_count = 0;
    s_status = ASSESSMENT_SYNC_STATUS_IDLE;
    s_last_success_epoch_seconds = 0;
    memset(&s_session, 0, sizeof(s_session));
    memset(s_answers, 0, sizeof(s_answers));
    s_answer_count = 0;
    s_has_session = false;
}

void assessment_sync_host_set_stop(bool stop) {
    s_stop = stop;
}

}

#endif
