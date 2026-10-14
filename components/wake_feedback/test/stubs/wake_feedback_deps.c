#include "diagnostic_reporter.h"
#include "esp_timer.h"
#include "led_indicator.h"
#include "prompt_tone.h"
#include "voice_wake.h"

#include <string.h>

// Controllable host stand-ins for the modules a wake event fans out to. Each
// downstream result is settable so the test can prove one failure does not
// suppress the others.
static bool host_voice_wake_ready = true;
static voice_wake_handler_t host_wake_handler;
static int64_t host_now_us = 12345678;

static prompt_tone_error_t host_cue_result = PROMPT_TONE_OK;
static int host_cue_calls;
static prompt_tone_t host_last_cue;

static led_indicator_error_t host_led_result = LED_INDICATOR_OK;
static int host_led_calls;
static led_indicator_state_t host_last_led_state;

static esp_err_t host_telemetry_result = ESP_OK;
static int host_telemetry_calls;
static char host_telemetry_event[32];
static char host_telemetry_detail[VOICE_WAKE_DETAIL_CODE_SIZE];
static uint32_t host_telemetry_duration;

int64_t esp_timer_get_time(void) {
    return host_now_us;
}

bool voice_wake_is_ready(void) {
    return host_voice_wake_ready;
}

esp_err_t voice_wake_set_wake_handler(
    voice_wake_handler_t handler,
    void *context
) {
    (void)context;
    host_wake_handler = handler;
    return ESP_OK;
}

prompt_tone_error_t prompt_tone_play(prompt_tone_t tone) {
    ++host_cue_calls;
    host_last_cue = tone;
    return host_cue_result;
}

const char *prompt_tone_error_name(prompt_tone_error_t error) {
    (void)error;
    return "prompt_tone_error";
}

led_indicator_error_t led_indicator_set_state(led_indicator_state_t state) {
    ++host_led_calls;
    host_last_led_state = state;
    return host_led_result;
}

const char *led_indicator_error_name(led_indicator_error_t error) {
    (void)error;
    return "led_indicator_error";
}

esp_err_t diagnostic_reporter_record_interaction(
    const char *event_type,
    const char *detail_code,
    uint32_t duration_ms
) {
    ++host_telemetry_calls;
    strncpy(host_telemetry_event, event_type, sizeof(host_telemetry_event) - 1);
    strncpy(host_telemetry_detail, detail_code, sizeof(host_telemetry_detail) - 1);
    host_telemetry_duration = duration_ms;
    return host_telemetry_result;
}

// Test hooks.
void wake_feedback_host_set_voice_wake_ready(bool ready) {
    host_voice_wake_ready = ready;
}

void wake_feedback_host_emit_wake(const voice_wake_event_t *event) {
    if (host_wake_handler != NULL) {
        host_wake_handler(event, NULL);
    }
}

void wake_feedback_host_set_now_us(int64_t now_us) {
    host_now_us = now_us;
}

void wake_feedback_host_set_cue_result(prompt_tone_error_t result) {
    host_cue_result = result;
}

int wake_feedback_host_cue_calls(void) {
    return host_cue_calls;
}

prompt_tone_t wake_feedback_host_last_cue(void) {
    return host_last_cue;
}

void wake_feedback_host_set_led_result(led_indicator_error_t result) {
    host_led_result = result;
}

int wake_feedback_host_led_calls(void) {
    return host_led_calls;
}

led_indicator_state_t wake_feedback_host_last_led_state(void) {
    return host_last_led_state;
}

void wake_feedback_host_set_telemetry_result(esp_err_t result) {
    host_telemetry_result = result;
}

int wake_feedback_host_telemetry_calls(void) {
    return host_telemetry_calls;
}

const char *wake_feedback_host_telemetry_event(void) {
    return host_telemetry_event;
}

const char *wake_feedback_host_telemetry_detail(void) {
    return host_telemetry_detail;
}

uint32_t wake_feedback_host_telemetry_duration(void) {
    return host_telemetry_duration;
}
