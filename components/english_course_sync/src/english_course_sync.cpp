#include "english_course_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "device_binding_client.h"
#include "device_identity.h"
#include "english_course.h"
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
#define ENGLISH_COURSE_SYNC_HAS_TRANSPORT_SECURITY 1
#else
#define ENGLISH_COURSE_SYNC_HAS_TRANSPORT_SECURITY 0
#endif

#ifndef CONFIG_ENGLISH_COURSE_SYNC_INTERVAL_SECONDS
#define CONFIG_ENGLISH_COURSE_SYNC_INTERVAL_SECONDS 3600
#endif
#ifndef CONFIG_ENGLISH_COURSE_SYNC_RETRY_INTERVAL_SECONDS
#define CONFIG_ENGLISH_COURSE_SYNC_RETRY_INTERVAL_SECONDS 15
#endif

#define ENGLISH_COURSE_SYNC_URL_SIZE 384
#define ENGLISH_COURSE_SYNC_RESPONSE_SIZE 65536
#define ENGLISH_COURSE_SYNC_REQUEST_SIZE 512

static const char *const TAG = "english_sync";
static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflowed;
} english_course_sync_response_t;

static esp_err_t english_course_sync_response_init(
    english_course_sync_response_t *response
) {
    response->body = (char *)malloc(ENGLISH_COURSE_SYNC_RESPONSE_SIZE);
    if (response->body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    response->capacity = ENGLISH_COURSE_SYNC_RESPONSE_SIZE;
    response->length = 0;
    response->overflowed = false;
    response->body[0] = '\0';
    return ESP_OK;
}

static void english_course_sync_response_destroy(
    english_course_sync_response_t *response
) {
    free(response->body);
    memset(response, 0, sizeof(*response));
}

static esp_err_t english_course_sync_http_event(
    esp_http_client_event_t *event
) {
    english_course_sync_response_t *response =
        (english_course_sync_response_t *)event->user_data;
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
// lesson fetch and the practice-attempt reports. The caller owns the token.
static esp_err_t english_course_sync_prepare(
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

static esp_err_t english_course_sync_fetch(JsonDocument *document) {
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[ENGLISH_COURSE_SYNC_URL_SIZE] = {0};
    if (english_course_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char url[ENGLISH_COURSE_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/english/lessons",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#if ENGLISH_COURSE_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_INVALID_ARG;
    }
#endif

    english_course_sync_response_t response = {};
    if (english_course_sync_response_init(&response) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 20000;
    config.event_handler = english_course_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        english_course_sync_response_destroy(&response);
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
        english_course_sync_response_destroy(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        english_course_sync_response_destroy(&response);
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code < 200 || status_code >= 300) {
        english_course_sync_response_destroy(&response);
        return ESP_FAIL;
    }
    if (response.overflowed ||
        deserializeJson(*document, response.body) != DeserializationError::Ok) {
        english_course_sync_response_destroy(&response);
        return ESP_ERR_INVALID_ARG;
    }
    english_course_sync_response_destroy(&response);
    return ESP_OK;
}

// Copy one JSON string into a fixed-size destination, keeping the field empty
// when the value is missing or too long to hold safely. Returns false when the
// value was copied.
static bool english_course_sync_copy_field(
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

static void english_course_sync_parse_lesson(
    JsonVariantConst value,
    english_course_lesson_t *lesson
) {
    memset(lesson, 0, sizeof(*lesson));
    if (!english_course_sync_copy_field(
            lesson->lesson_id,
            sizeof(lesson->lesson_id),
            value["lesson_id"] | ""
        )) {
        return;
    }
    english_course_sync_copy_field(
        lesson->title,
        sizeof(lesson->title),
        value["title"] | ""
    );
    english_course_sync_copy_field(
        lesson->age_tier,
        sizeof(lesson->age_tier),
        value["age_tier"] | ""
    );

    JsonArrayConst words = value["words"].as<JsonArrayConst>();
    if (!words.isNull()) {
        for (JsonVariantConst word : words) {
            if (lesson->word_count >= ENGLISH_COURSE_WORDS_PER_LESSON) {
                break;
            }
            english_course_word_t *slot = &lesson->words[lesson->word_count];
            if (!english_course_sync_copy_field(
                    slot->item_id,
                    sizeof(slot->item_id),
                    word["id"] | ""
                ) ||
                !english_course_sync_copy_field(
                    slot->term,
                    sizeof(slot->term),
                    word["term"] | ""
                )) {
                continue;
            }
            english_course_sync_copy_field(
                slot->phonetic,
                sizeof(slot->phonetic),
                word["phonetic"] | ""
            );
            english_course_sync_copy_field(
                slot->translation,
                sizeof(slot->translation),
                word["translation"] | ""
            );
            english_course_sync_copy_field(
                slot->example,
                sizeof(slot->example),
                word["example"] | ""
            );
            lesson->word_count += 1;
        }
    }

    JsonArrayConst sentences = value["sentences"].as<JsonArrayConst>();
    if (!sentences.isNull()) {
        for (JsonVariantConst sentence : sentences) {
            if (lesson->sentence_count >=
                ENGLISH_COURSE_SENTENCES_PER_LESSON) {
                break;
            }
            english_course_sentence_t *slot =
                &lesson->sentences[lesson->sentence_count];
            if (!english_course_sync_copy_field(
                    slot->item_id,
                    sizeof(slot->item_id),
                    sentence["id"] | ""
                ) ||
                !english_course_sync_copy_field(
                    slot->pattern,
                    sizeof(slot->pattern),
                    sentence["pattern"] | ""
                )) {
                continue;
            }
            english_course_sync_copy_field(
                slot->prompt,
                sizeof(slot->prompt),
                sentence["prompt"] | ""
            );
            english_course_sync_copy_field(
                slot->model_answer,
                sizeof(slot->model_answer),
                sentence["model_answer"] | ""
            );
            lesson->sentence_count += 1;
        }
    }
}

static esp_err_t english_course_sync_apply_document(JsonDocument &document) {
    JsonArrayConst lessons = document["data"]["lessons"].as<JsonArrayConst>();
    if (lessons.isNull()) {
        return ESP_ERR_INVALID_ARG;
    }
    // The parsed catalog is too large for the worker stack, so it lives on the
    // heap. The capacity matches the cache so no valid lesson is dropped.
    english_course_lesson_t *parsed =
        (english_course_lesson_t *)calloc(
            ENGLISH_COURSE_LESSON_CAPACITY,
            sizeof(english_course_lesson_t)
        );
    if (parsed == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t lesson_count = 0;
    size_t entry_count = 0;
    for (JsonVariantConst value : lessons) {
        entry_count += 1;
        if (lesson_count >= ENGLISH_COURSE_LESSON_CAPACITY) {
            continue;
        }
        english_course_lesson_t *lesson = &parsed[lesson_count];
        english_course_sync_parse_lesson(value, lesson);
        if (lesson->lesson_id[0] == '\0' ||
            (lesson->word_count == 0 && lesson->sentence_count == 0)) {
            memset(lesson, 0, sizeof(*lesson));
            continue;
        }
        lesson_count += 1;
    }
    esp_err_t result = ESP_OK;
    if (lesson_count == 0 && entry_count > 0) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        result = english_course_replace_lessons(parsed, lesson_count);
    }
    free(parsed);
    return result;
}

// Report one practice attempt to the platform. Returns ESP_OK once the platform
// accepts it so the caller can drop it from the queue. The platform rejects an
// attempt whose lesson id is unknown, untrusted text is unusable, or whose
// target text is empty; those are permanent failures, so a 404/422 is reported
// as ESP_ERR_NOT_FOUND to let the caller discard the stale attempt instead of
// retrying it forever. Any other result keeps the attempt queued.
static esp_err_t english_course_sync_report_attempt(
    const english_course_attempt_t *attempt,
    const char *session_token,
    const char *device_id,
    const char *base_url
) {
    if (attempt->target_text[0] == '\0') {
        // A reportable attempt always carries the target text; drop a row that
        // somehow lacks it rather than sending a request the platform rejects.
        return ESP_ERR_NOT_FOUND;
    }
    char url[ENGLISH_COURSE_SYNC_URL_SIZE] = {0};
    const int written = snprintf(
        url,
        sizeof(url),
        "%s/api/v1/devices/%s/english/attempts",
        base_url,
        device_id
    );
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }
#if ENGLISH_COURSE_SYNC_HAS_TRANSPORT_SECURITY
    if (transport_security_validate_outbound(url, NULL) !=
        TRANSPORT_SECURITY_OK) {
        return ESP_ERR_INVALID_ARG;
    }
#endif

    JsonDocument payload;
    payload["child_id"] = attempt->child_id;
    payload["lesson_id"] = attempt->lesson_id;
    payload["item_id"] = attempt->item_id;
    payload["item_type"] = english_course_item_type_name(attempt->item_type);
    payload["target_text"] = attempt->target_text;
    payload["recognized_text"] = attempt->recognized_text;
    payload["score"] = attempt->score;
    char request_body[ENGLISH_COURSE_SYNC_REQUEST_SIZE] = {0};
    const size_t body_length =
        serializeJson(payload, request_body, sizeof(request_body));
    if (body_length == 0 || body_length >= sizeof(request_body)) {
        return ESP_ERR_INVALID_ARG;
    }

    english_course_sync_response_t response = {};
    if (english_course_sync_response_init(&response) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.event_handler = english_course_sync_http_event;
    config.user_data = &response;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        english_course_sync_response_destroy(&response);
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
    english_course_sync_response_destroy(&response);
    if (transport_result != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (status_code == 401 || status_code == 403) {
        (void)device_binding_client_clear_session_token();
        return ESP_ERR_INVALID_STATE;
    }
    // 404 (unknown lesson) and 422 (an attempt the platform cannot accept) are
    // permanent, so drop the row instead of retrying it forever.
    if (status_code == 404 || status_code == 422) {
        return ESP_ERR_NOT_FOUND;
    }
    if (status_code < 200 || status_code >= 300) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Drain the queued practice attempts, stopping at the first transient failure
// so the rest of the queue survives for the next cycle.
static void english_course_sync_report_attempts(void) {
    if (english_course_pending_attempt_count() == 0) {
        return;
    }
    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[ENGLISH_COURSE_SYNC_URL_SIZE] = {0};
    if (english_course_sync_prepare(
            session_token,
            sizeof(session_token),
            device_id,
            sizeof(device_id),
            base_url,
            sizeof(base_url)
        ) != ESP_OK) {
        return;
    }
    english_course_attempt_t attempts[ENGLISH_COURSE_OUTBOUND_CAPACITY] = {};
    size_t attempt_count = 0;
    if (english_course_list_pending_attempts(
            attempts,
            ENGLISH_COURSE_OUTBOUND_CAPACITY,
            &attempt_count
        ) != ESP_OK) {
        memset(session_token, 0, sizeof(session_token));
        return;
    }
    for (size_t index = 0; index < attempt_count; ++index) {
        const esp_err_t report_result = english_course_sync_report_attempt(
            &attempts[index],
            session_token,
            device_id,
            base_url
        );
        if (report_result == ESP_OK || report_result == ESP_ERR_NOT_FOUND) {
            // A permanently rejected attempt is dropped rather than retried
            // forever.
            (void)english_course_ack_attempt(
                attempts[index].lesson_id,
                attempts[index].item_id,
                attempts[index].practiced_at_epoch_seconds
            );
        } else {
            ESP_LOGW(
                TAG,
                "english attempt report failed for %s: %s",
                attempts[index].item_id,
                esp_err_to_name(report_result)
            );
            break;
        }
    }
    memset(session_token, 0, sizeof(session_token));
}

esp_err_t english_course_sync_now(void) {
    if (!english_course_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    JsonDocument document;
    const esp_err_t fetch_result = english_course_sync_fetch(&document);
    esp_err_t apply_result = fetch_result;
    if (fetch_result == ESP_OK) {
        apply_result = english_course_sync_apply_document(document);
        if (apply_result != ESP_OK) {
            ESP_LOGW(TAG, "english lesson response rejected: %s",
                     esp_err_to_name(apply_result));
        }
    }
    // Reporting queued practice is independent of the catalog refresh, so a
    // transient lesson-fetch failure never blocks a child's progress upload.
    english_course_sync_report_attempts();
    return apply_result;
}

static void english_course_sync_worker(void *argument) {
    (void)argument;
    while (!s_stop) {
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (network_manager_get_state() == NETWORK_MANAGER_STATE_CONNECTED &&
            time_sync_is_synchronized() &&
            device_binding_client_is_registered()) {
            result = english_course_sync_now();
            if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(
                    TAG,
                    "english lesson sync failed: %s",
                    esp_err_to_name(result)
                );
            }
        }
        const uint32_t delay_seconds = result == ESP_ERR_INVALID_STATE
            ? CONFIG_ENGLISH_COURSE_SYNC_RETRY_INTERVAL_SECONDS
            : CONFIG_ENGLISH_COURSE_SYNC_INTERVAL_SECONDS;
        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000));
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t english_course_sync_initialize(void) {
    if (!english_course_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_stop = false;
    if (xTaskCreate(
            english_course_sync_worker,
            "english_sync",
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

const module_descriptor_t *english_course_sync_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "english_course_sync",
        .version = "1.0.0",
        .initialize = english_course_sync_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}

#ifdef ENGLISH_COURSE_SYNC_HOST_TESTS
extern "C" {

bool english_course_sync_host_is_ready(void) {
    return s_ready;
}

void english_course_sync_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
}

void english_course_sync_host_set_stop(bool stop) {
    s_stop = stop;
}

}
#endif
