#include "wake_feedback.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#if CONFIG_FEATURE_VOICE_WAKE
#include "voice_wake.h"
#endif
#if CONFIG_FEATURE_PROMPT_TONE
#include "prompt_tone.h"
#endif
#if CONFIG_FEATURE_LED_INDICATOR
#include "led_indicator.h"
#endif
#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER
#include "diagnostic_reporter.h"
#endif

static const char *const TAG = "wake_feedback";

static atomic_bool wake_feedback_ready;
static atomic_uint wake_feedback_accepted_events;
static atomic_uint wake_feedback_rejected_events;
static atomic_uint wake_feedback_cue_failures;
static atomic_uint wake_feedback_led_failures;
static atomic_uint wake_feedback_telemetry_failures;
static atomic_ullong wake_feedback_last_wake_at_ms;

static uint64_t wake_feedback_now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000);
}

// Runs on the voice_wake detector task, so it only performs bounded,
// non-blocking work and never starts or stops the microphone.
static void wake_feedback_on_wake(
    const voice_wake_event_t *event,
    void *context
) {
    (void)context;
    if (event == NULL) {
        atomic_fetch_add(&wake_feedback_rejected_events, 1u);
        return;
    }

    atomic_fetch_add(&wake_feedback_accepted_events, 1u);
    atomic_store(&wake_feedback_last_wake_at_ms, wake_feedback_now_ms());

#if CONFIG_FEATURE_PROMPT_TONE
    const prompt_tone_error_t cue_result =
        prompt_tone_play(PROMPT_TONE_WAKE_ACCEPTED);
    if (cue_result != PROMPT_TONE_OK) {
        atomic_fetch_add(&wake_feedback_cue_failures, 1u);
        ESP_LOGW(TAG, "wake cue unavailable: %s",
                 prompt_tone_error_name(cue_result));
    }
#endif

#if CONFIG_FEATURE_LED_INDICATOR
    const led_indicator_error_t led_result =
        led_indicator_set_state(LED_INDICATOR_STATE_LISTENING);
    if (led_result != LED_INDICATOR_OK) {
        atomic_fetch_add(&wake_feedback_led_failures, 1u);
        ESP_LOGW(TAG, "wake indicator unavailable: %s",
                 led_indicator_error_name(led_result));
    }
#endif

#if CONFIG_FEATURE_DIAGNOSTIC_REPORTER
    const char *detail_code = event->wake_word_name[0] != '\0'
        ? event->wake_word_name
        : "unknown";
    const esp_err_t telemetry_result =
        diagnostic_reporter_record_interaction(
            "wake_detected",
            detail_code,
            event->confidence_milli
        );
    if (telemetry_result != ESP_OK) {
        atomic_fetch_add(&wake_feedback_telemetry_failures, 1u);
        ESP_LOGW(TAG, "wake telemetry unavailable: %s",
                 esp_err_to_name(telemetry_result));
    }
#endif
}

esp_err_t wake_feedback_init(void) {
    if (atomic_load(&wake_feedback_ready)) {
        return ESP_OK;
    }
#if !CONFIG_FEATURE_VOICE_WAKE
    return ESP_ERR_INVALID_STATE;
#else
    if (!voice_wake_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = voice_wake_set_wake_handler(
        wake_feedback_on_wake,
        NULL
    );
    if (result != ESP_OK) {
        return result;
    }
    atomic_store(&wake_feedback_ready, true);
    ESP_LOGI(TAG, "wake feedback ready");
    return ESP_OK;
#endif
}

bool wake_feedback_is_ready(void) {
    return atomic_load(&wake_feedback_ready);
}

wake_feedback_snapshot_t wake_feedback_get_snapshot(void) {
    wake_feedback_snapshot_t snapshot = {
        .accepted_events =
            atomic_load(&wake_feedback_accepted_events),
        .rejected_events =
            atomic_load(&wake_feedback_rejected_events),
        .cue_failures = atomic_load(&wake_feedback_cue_failures),
        .led_failures = atomic_load(&wake_feedback_led_failures),
        .telemetry_failures =
            atomic_load(&wake_feedback_telemetry_failures),
        .last_wake_at_ms = atomic_load(&wake_feedback_last_wake_at_ms),
    };
    return snapshot;
}

const char *wake_feedback_error_name(wake_feedback_error_t error) {
    switch (error) {
        case WAKE_FEEDBACK_OK:
            return "ok";
        case WAKE_FEEDBACK_ERR_NOT_INITIALIZED:
            return "wake_feedback_not_initialized";
        case WAKE_FEEDBACK_ERR_NOT_READY:
        default:
            return "wake_feedback_not_ready";
    }
}

static void wake_feedback_shutdown(void) {
#if CONFIG_FEATURE_VOICE_WAKE
    if (voice_wake_is_ready()) {
        (void)voice_wake_set_wake_handler(NULL, NULL);
    }
#endif
    atomic_store(&wake_feedback_ready, false);
}

const module_descriptor_t *wake_feedback_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "wake_feedback",
        .version = "1.0.0",
        .initialize = wake_feedback_init,
        .shutdown = wake_feedback_shutdown,
    };
    return &descriptor;
}
