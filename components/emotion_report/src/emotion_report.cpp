#include "emotion_report.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

#include "emotion_report_core.h"

#if CONFIG_FEATURE_TRANSPORT_SECURITY && __has_include("transport_security.h")
#include "transport_security.h"
#define EMOTION_REPORT_HAS_TRANSPORT_SECURITY 1
#else
#define EMOTION_REPORT_HAS_TRANSPORT_SECURITY 0
#endif

// Kconfig defaults mirror the fallbacks in every sibling sync component so a
// host test build without a generated sdkconfig still compiles.
#ifndef CONFIG_EMOTION_REPORT_INTERVAL_SECONDS
#define CONFIG_EMOTION_REPORT_INTERVAL_SECONDS 60
#endif
#ifndef CONFIG_EMOTION_REPORT_RETRY_INTERVAL_SECONDS
#define CONFIG_EMOTION_REPORT_RETRY_INTERVAL_SECONDS 30
#endif
#ifndef CONFIG_EMOTION_REPORT_BACKOFF_MAX_SECONDS
#define CONFIG_EMOTION_REPORT_BACKOFF_MAX_SECONDS 900
#endif
#ifndef CONFIG_EMOTION_REPORT_QUEUE_CAPACITY
#define CONFIG_EMOTION_REPORT_QUEUE_CAPACITY 8
#endif

// A zero capacity would compile a zero-length array; one slot keeps the
// "queue is full, drop the oldest" path testable in every profile.
#define EMOTION_REPORT_QUEUE_SLOTS \
    (CONFIG_EMOTION_REPORT_QUEUE_CAPACITY > 0 \
        ? CONFIG_EMOTION_REPORT_QUEUE_CAPACITY \
        : 1)

// A UUID device id leaves plenty of room below this bound, so a longer base
// URL is rejected rather than truncated into a different endpoint.
#define EMOTION_REPORT_URL_SIZE 384
// A fixed-size worker avoids heap churn on a device that reports for years.
#define EMOTION_REPORT_TASK_STACK_SIZE 6144
#define EMOTION_REPORT_TASK_PRIORITY 2
// The worker only has to notice a queued sample promptly; the interval gate,
// not the poll period, decides when a request is actually sent.
#define EMOTION_REPORT_MAX_POLL_SECONDS 15

// A child produces several turns a minute; queuing every one of them would
// only fill the queue with samples the interval gate can never send.
#define EMOTION_REPORT_DUPLICATE_WINDOW_SECONDS 10

static const char *const TAG = "emotion_report";

static bool s_ready;
static volatile bool s_stop;
static TaskHandle_t s_task_handle;
static SemaphoreHandle_t s_state_lock;
static emotion_report_observation_t s_queue[EMOTION_REPORT_QUEUE_SLOTS];
static size_t s_queue_count;
static emotion_report_upload_t s_last_upload;
static bool s_family_disabled;
static int64_t s_last_attempt_epoch_seconds;
static int64_t s_retry_not_before_epoch_seconds;
static uint32_t s_consecutive_failures;

// Single clock seam for the whole module. On the device it reads the system
// clock; a host test replaces it, because the sampling interval and the retry
// backoff are the module's own rules and must be exercised without waiting out
// real minutes.
#ifdef EMOTION_REPORT_HOST_TESTS
extern "C" int64_t emotion_report_host_now_epoch_seconds(void);
#endif

static int64_t emotion_report_now_epoch_seconds(void) {
#ifdef EMOTION_REPORT_HOST_TESTS
    return emotion_report_host_now_epoch_seconds();
#else
    return (int64_t)time(NULL);
#endif
}

// Milliseconds until the next instant the worker has something to do. It must
// never exceed the poll period, so a sample queued while the worker sleeps is
// noticed without waiting out a long interval.
static int64_t emotion_report_seconds_until(int64_t target_epoch_seconds) {
    const int64_t now = emotion_report_now_epoch_seconds();
    const int64_t remaining = target_epoch_seconds > now
        ? target_epoch_seconds - now
        : 0;
    return remaining < EMOTION_REPORT_MAX_POLL_SECONDS
        ? remaining
        : EMOTION_REPORT_MAX_POLL_SECONDS;
}

static void emotion_report_drop_oldest_locked(void) {
    if (s_queue_count == 0) {
        return;
    }
    memmove(
        &s_queue[0],
        &s_queue[1],
        (s_queue_count - 1) * sizeof(s_queue[0])
    );
    s_queue_count -= 1;
    memset(&s_queue[s_queue_count], 0, sizeof(s_queue[0]));
}

static bool emotion_report_ensure_lock(void) {
    if (s_state_lock != NULL) {
        return true;
    }
    s_state_lock = xSemaphoreCreateMutex();
    return s_state_lock != NULL;
}

esp_err_t emotion_report_observe(
    companionship_emotion_t emotion,
    int64_t observed_at_epoch_seconds
) {
    if (!s_ready || !emotion_report_ensure_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    // Registration is a local persisted flag, so it is safe to read from a
    // caller task and it keeps an unregistered device from filling the queue
    // with samples it could never send.
    if (!device_binding_client_is_registered()) {
        return ESP_ERR_INVALID_STATE;
    }
    emotion_report_observation_t observation = {};
    const esp_err_t make_result = emotion_report_core_make_observation(
        emotion,
        observed_at_epoch_seconds,
        NULL,
        &observation
    );
    if (make_result != ESP_OK) {
        return make_result;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    if (s_family_disabled) {
        xSemaphoreGive(s_state_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_queue_count > 0) {
        const emotion_report_observation_t *newest =
            &s_queue[s_queue_count - 1];
        const int64_t own_age = observed_at_epoch_seconds -
            newest->observed_at_epoch_seconds;
        if (strcmp(newest->label, observation.label) == 0 &&
            own_age >= 0 &&
            own_age < EMOTION_REPORT_DUPLICATE_WINDOW_SECONDS) {
            xSemaphoreGive(s_state_lock);
            return ESP_OK;
        }
    }
    if (s_queue_count >= EMOTION_REPORT_QUEUE_SLOTS) {
        // Keeping the newest sample preserves the recent distribution; the
        // oldest instant is the least useful and the most stale child data.
        emotion_report_drop_oldest_locked();
    }
    s_queue[s_queue_count] = observation;
    s_queue_count += 1;
    xSemaphoreGive(s_state_lock);
    return ESP_OK;
}

bool emotion_report_is_due(void) {
    if (!s_ready || !emotion_report_ensure_lock()) {
        return false;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const bool is_due = s_queue_count > 0 && !s_family_disabled;
    xSemaphoreGive(s_state_lock);
    return is_due;
}

emotion_report_upload_t emotion_report_last_upload(void) {
    if (s_state_lock == NULL) {
        return EMOTION_REPORT_UPLOAD_IDLE;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const emotion_report_upload_t last = s_last_upload;
    xSemaphoreGive(s_state_lock);
    return last;
}

bool emotion_report_is_disabled_by_family(void) {
    if (s_state_lock == NULL) {
        return false;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const bool is_disabled = s_family_disabled;
    xSemaphoreGive(s_state_lock);
    return is_disabled;
}

// Resolve the session token, device id and base url. The caller owns and must
// clear the token.
static esp_err_t emotion_report_prepare(
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

// Send the body and translate the response. A 401/403 drops the local token so
// the next cycle performs a fresh challenge instead of retrying a revoked
// credential. Only the status code is used: the acknowledgement body carries
// nothing this device needs, so it is never parsed into RAM.
static emotion_report_upload_t emotion_report_post(
    const char *url,
    const char *session_token,
    const char *body,
    int body_length
) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return EMOTION_REPORT_UPLOAD_RETRY;
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
    esp_http_client_set_post_field(client, body, body_length);
    const esp_err_t transport_result = esp_http_client_perform(client);
    const int status_code = transport_result == ESP_OK
        ? esp_http_client_get_status_code(client)
        : 0;
    esp_http_client_cleanup(client);
    memset(authorization, 0, sizeof(authorization));
    if (status_code == 401 || status_code == 403) {
        (void)device_binding_client_clear_session_token();
    }
    return emotion_report_core_classify_status(status_code);
}

// Whether the interval and the failure backoff allow an attempt right now.
// Checked under the lock and before any network preparation, so a caller that
// flushes after every turn pays nothing while the worker is waiting.
static bool emotion_report_gate_is_open_locked(int64_t now) {
    if (s_queue_count == 0 || s_family_disabled) {
        return false;
    }
    if (now < s_retry_not_before_epoch_seconds) {
        return false;
    }
    return s_last_attempt_epoch_seconds <= 0 ||
        now - s_last_attempt_epoch_seconds >=
            CONFIG_EMOTION_REPORT_INTERVAL_SECONDS;
}

esp_err_t emotion_report_flush(void) {
    if (!s_ready || !emotion_report_ensure_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    emotion_report_observation_t observation = {};
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const int64_t attempt_started_epoch_seconds =
        emotion_report_now_epoch_seconds();
    const bool is_open = emotion_report_gate_is_open_locked(
        attempt_started_epoch_seconds
    );
    // Copy the head while the lock is held: the network preparation below
    // releases it and the queue may change in the meantime.
    if (is_open) {
        observation = s_queue[0];
    }
    xSemaphoreGive(s_state_lock);
    if (!is_open) {
        return ESP_OK;
    }

    char session_token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    char device_id[DEVICE_IDENTIFIER_SIZE] = {0};
    char base_url[EMOTION_REPORT_URL_SIZE] = {0};
    const esp_err_t prepare_result = emotion_report_prepare(
        session_token,
        sizeof(session_token),
        device_id,
        sizeof(device_id),
        base_url,
        sizeof(base_url)
    );
    emotion_report_upload_t upload = EMOTION_REPORT_UPLOAD_DEFERRED;
    if (prepare_result == ESP_OK) {
        char url[EMOTION_REPORT_URL_SIZE] = {0};
        char body[EMOTION_REPORT_BODY_SIZE] = {0};
        const int url_length = snprintf(
            url,
            sizeof(url),
            "%s/api/v1/devices/%s/emotion-observations",
            base_url,
            device_id
        );
        const int body_length = emotion_report_core_build_body(
            &observation,
            body,
            sizeof(body)
        );
        bool is_sendable = url_length > 0 &&
            (size_t)url_length < sizeof(url) &&
            body_length > 0;
#if EMOTION_REPORT_HAS_TRANSPORT_SECURITY
        if (is_sendable &&
            transport_security_validate_outbound(url, NULL) !=
                TRANSPORT_SECURITY_OK) {
            is_sendable = false;
        }
#endif
        if (!is_sendable) {
            upload = EMOTION_REPORT_UPLOAD_REJECTED;
        } else {
            upload = emotion_report_post(
                url,
                session_token,
                body,
                body_length
            );
        }
        memset(body, 0, sizeof(body));
    }
    memset(session_token, 0, sizeof(session_token));

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    // Re-read the clock for the retry backoff, which must start when the failure
    // was observed rather than when the request began.
    const int64_t settled_now = emotion_report_now_epoch_seconds();
    s_last_upload = upload;
    if (upload == EMOTION_REPORT_UPLOAD_OK ||
        upload == EMOTION_REPORT_UPLOAD_REJECTED) {
        // Compare the head first: a concurrent observe may have dropped it
        // while the request was in flight, and the next sample must not be
        // discarded on behalf of the one that just settled.
        if (s_queue_count > 0 &&
            memcmp(&s_queue[0], &observation, sizeof(observation)) == 0) {
            emotion_report_drop_oldest_locked();
        }
        s_consecutive_failures = 0;
        s_retry_not_before_epoch_seconds = 0;
    }
    if (upload == EMOTION_REPORT_UPLOAD_OK) {
        // Stamped with the attempt's start: the interval bounds how often the
        // device reports, and charging the request's own duration against the
        // next window would let a slow link silently double the gap between
        // two samples the guardian is entitled to.
        s_last_attempt_epoch_seconds = attempt_started_epoch_seconds;
    } else if (upload == EMOTION_REPORT_UPLOAD_RETRY) {
        // A persistent failure is a property of the link, not of one sample,
        // so the wait doubles until the ceiling instead of retrying every turn.
        s_consecutive_failures = s_consecutive_failures < 31
            ? s_consecutive_failures + 1
            : 31;
        int64_t delay = CONFIG_EMOTION_REPORT_RETRY_INTERVAL_SECONDS;
        for (uint32_t index = 1;
             index < s_consecutive_failures &&
             delay < CONFIG_EMOTION_REPORT_BACKOFF_MAX_SECONDS;
             ++index) {
            delay *= 2;
        }
        if (delay > CONFIG_EMOTION_REPORT_BACKOFF_MAX_SECONDS) {
            delay = CONFIG_EMOTION_REPORT_BACKOFF_MAX_SECONDS;
        }
        s_retry_not_before_epoch_seconds = settled_now + delay;
    }
    xSemaphoreGive(s_state_lock);

    if (upload == EMOTION_REPORT_UPLOAD_OK) {
        return ESP_OK;
    }
    // A rejected sample was settled locally and clears itself; only a
    // retryable failure is reported so the worker knows to back off.
    if (upload == EMOTION_REPORT_UPLOAD_RETRY) {
        return ESP_FAIL;
    }
    // Deferred means the request was never built. An insecure endpoint is a
    // configuration defect the caller must see, while an absent link is a state
    // the device waits out, so the two are not flattened into one code.
    if (upload == EMOTION_REPORT_UPLOAD_DEFERRED) {
        return prepare_result == ESP_OK ? ESP_ERR_INVALID_STATE : prepare_result;
    }
    return ESP_OK;
}

// One pass of the worker: attempt an upload, then wait for whatever the module
// decided comes next. Split out so a host test can run a single pass without the
// loop's exit condition swallowing it.
static void emotion_report_worker_iteration(bool *has_warned) {
    const esp_err_t result = emotion_report_flush();
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const bool is_retrying = s_consecutive_failures > 0;
    const int64_t wait_seconds = is_retrying
        ? emotion_report_seconds_until(s_retry_not_before_epoch_seconds)
        : (s_last_attempt_epoch_seconds > 0
            ? emotion_report_seconds_until(
                s_last_attempt_epoch_seconds +
                    CONFIG_EMOTION_REPORT_INTERVAL_SECONDS
            )
            : 1);
    xSemaphoreGive(s_state_lock);
    if (result == ESP_FAIL && !*has_warned) {
        ESP_LOGW(TAG, "upload deferred, retrying with backoff");
        *has_warned = true;
    } else if (result == ESP_OK) {
        *has_warned = false;
    }
    const int64_t bounded_seconds = wait_seconds < 1 ? 1 : wait_seconds;
    vTaskDelay(pdMS_TO_TICKS(bounded_seconds * 1000));
}

static void emotion_report_worker(void *argument) {
    (void)argument;
    // One line per failure streak: the device may be offline for days, and a
    // warning per attempt would be the log spam the contract forbids.
    bool has_warned = false;
    while (!s_stop) {
        emotion_report_worker_iteration(&has_warned);
    }
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

#ifdef EMOTION_REPORT_HOST_TESTS
extern "C" {

/** @brief Host-test hook: queue depth, for assertions on the bounded queue. */
size_t emotion_report_host_queue_count(void) {
    if (s_state_lock == NULL) {
        return 0;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const size_t count = s_queue_count;
    xSemaphoreGive(s_state_lock);
    return count;
}

/** @brief Host-test hook: the label of one queued sample, or "". */
const char *emotion_report_host_queue_label(size_t index) {
    static const char empty[] = "";
    if (s_state_lock == NULL) {
        return empty;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const char *label = index < s_queue_count ? s_queue[index].label : empty;
    xSemaphoreGive(s_state_lock);
    return label;
}

/** @brief Host-test hook: the child id of one queued sample, or "". */
const char *emotion_report_host_queue_child_id(size_t index) {
    static const char empty[] = "";
    if (s_state_lock == NULL) {
        return empty;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const char *child_id = index < s_queue_count
        ? s_queue[index].child_id
        : empty;
    xSemaphoreGive(s_state_lock);
    return child_id;
}

/** @brief Host-test hook: the fixed confidence actually sent. */
int emotion_report_host_queue_confidence(size_t index) {
    if (s_state_lock == NULL) {
        return -1;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const int confidence = index < s_queue_count
        ? s_queue[index].confidence
        : -1;
    xSemaphoreGive(s_state_lock);
    return confidence;
}

/** @brief Host-test hook: reset the runtime between scenarios. */
void emotion_report_host_reset_module(void) {
    s_ready = false;
    s_stop = false;
    s_task_handle = NULL;
    // The mutex is created on first use and its stub is a single static
    // object, so it is dropped together with the state it guards; keeping the
    // handle would leave the next initialize without a lock.
    s_state_lock = NULL;
    memset(s_queue, 0, sizeof(s_queue));
    s_queue_count = 0;
    s_last_upload = EMOTION_REPORT_UPLOAD_IDLE;
    s_family_disabled = false;
    s_last_attempt_epoch_seconds = 0;
    s_retry_not_before_epoch_seconds = 0;
    s_consecutive_failures = 0;
}

/** @brief Host-test hook: force the consecutive-failure counter. */
void emotion_report_host_set_consecutive_failures(uint32_t failures) {
    s_consecutive_failures = failures;
}

/** @brief Host-test hook: the current consecutive-failure counter. */
uint32_t emotion_report_host_consecutive_failures(void) {
    return s_consecutive_failures;
}

/** @brief Host-test hook: the earliest instant the next attempt may run. */
int64_t emotion_report_host_retry_not_before(void) {
    return s_retry_not_before_epoch_seconds;
}

/** @brief Host-test hook: force the family-disabled state. */
void emotion_report_host_set_family_disabled(bool is_disabled) {
    s_family_disabled = is_disabled;
}

/** @brief Host-test hook: stop the worker loop before entering it. */
void emotion_report_host_set_stop(bool stop) {
    s_stop = stop;
}

/** @brief Host-test hook: run the worker body exactly once. */
void emotion_report_host_worker_iteration(void) {
    bool has_warned = false;
    emotion_report_worker_iteration(&has_warned);
}

}  // extern "C"
#endif

static esp_err_t emotion_report_initialize(void) {
    if (s_ready) {
        return ESP_OK;
    }
    if (!emotion_report_ensure_lock()) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    memset(s_queue, 0, sizeof(s_queue));
    s_queue_count = 0;
    s_last_upload = EMOTION_REPORT_UPLOAD_IDLE;
    s_family_disabled = false;
    s_last_attempt_epoch_seconds = 0;
    s_retry_not_before_epoch_seconds = 0;
    s_consecutive_failures = 0;
    xSemaphoreGive(s_state_lock);
    s_stop = false;
    if (xTaskCreate(
            emotion_report_worker,
            "emotion_report",
            EMOTION_REPORT_TASK_STACK_SIZE,
            NULL,
            EMOTION_REPORT_TASK_PRIORITY,
            &s_task_handle
        ) != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    return ESP_OK;
}

const module_descriptor_t *emotion_report_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "emotion_report",
        .version = "1.0.0",
        .initialize = emotion_report_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
