#include "wake_feedback.h"

#include "diagnostic_reporter.h"
#include "led_indicator.h"
#include "prompt_tone.h"
#include "voice_wake.h"

#include <assert.h>
#include <string.h>

void wake_feedback_host_set_voice_wake_ready(bool ready);
void wake_feedback_host_emit_wake(const voice_wake_event_t *event);
void wake_feedback_host_set_now_us(int64_t now_us);
void wake_feedback_host_set_cue_result(prompt_tone_error_t result);
int wake_feedback_host_cue_calls(void);
prompt_tone_t wake_feedback_host_last_cue(void);
void wake_feedback_host_set_led_result(led_indicator_error_t result);
int wake_feedback_host_led_calls(void);
led_indicator_state_t wake_feedback_host_last_led_state(void);
void wake_feedback_host_set_telemetry_result(esp_err_t result);
int wake_feedback_host_telemetry_calls(void);
const char *wake_feedback_host_telemetry_event(void);
const char *wake_feedback_host_telemetry_detail(void);
uint32_t wake_feedback_host_telemetry_duration(void);

static void test_init_requires_a_ready_detector(void) {
    wake_feedback_host_set_voice_wake_ready(false);
    assert(wake_feedback_init() == ESP_ERR_INVALID_STATE);
    assert(!wake_feedback_is_ready());
}

static void test_init_registers_once(void) {
    wake_feedback_host_set_voice_wake_ready(true);
    assert(wake_feedback_init() == ESP_OK);
    assert(wake_feedback_is_ready());
    /* Idempotent: a second init must not re-register the handler. */
    assert(wake_feedback_init() == ESP_OK);
}

static void test_accepted_wake_fans_out_to_all_consumers(void) {
    voice_wake_event_t event = {0};
    wake_feedback_snapshot_t snapshot;

    wake_feedback_host_set_cue_result(PROMPT_TONE_OK);
    wake_feedback_host_set_led_result(LED_INDICATOR_OK);
    wake_feedback_host_set_telemetry_result(ESP_OK);
    wake_feedback_host_set_now_us(5000000); /* 5000 ms */

    strncpy(
        event.detail_code,
        "wake_high_conf",
        sizeof(event.detail_code) - 1
    );
    wake_feedback_host_emit_wake(&event);

    snapshot = wake_feedback_get_snapshot();
    assert(snapshot.accepted_events == 1u);
    assert(snapshot.rejected_events == 0u);
    assert(snapshot.cue_failures == 0u);
    assert(snapshot.led_failures == 0u);
    assert(snapshot.telemetry_failures == 0u);
    assert(snapshot.last_wake_at_ms == 5000u);

    assert(wake_feedback_host_cue_calls() == 1);
    assert(wake_feedback_host_last_cue() == PROMPT_TONE_WAKE_ACCEPTED);
    assert(wake_feedback_host_led_calls() == 1);
    assert(
        wake_feedback_host_last_led_state() == LED_INDICATOR_STATE_LISTENING
    );
    assert(wake_feedback_host_telemetry_calls() == 1);
    assert(strcmp(wake_feedback_host_telemetry_event(), "wake_detected") == 0);
    assert(strcmp(wake_feedback_host_telemetry_detail(), "wake_high_conf") == 0);
    assert(wake_feedback_host_telemetry_duration() == 0u);
}

static void test_empty_detail_code_uses_unknown_fallback(void) {
    voice_wake_event_t event = {0};
    wake_feedback_host_emit_wake(&event);
    /* A blank detail must not be sent to the platform as an empty string. */
    assert(
        strcmp(wake_feedback_host_telemetry_detail(), "wake_unknown") == 0
    );
}

static void test_null_event_is_rejected(void) {
    wake_feedback_snapshot_t before = wake_feedback_get_snapshot();
    wake_feedback_snapshot_t after;
    wake_feedback_host_emit_wake(NULL);

    after = wake_feedback_get_snapshot();
    assert(after.rejected_events == before.rejected_events + 1);
    assert(after.accepted_events == before.accepted_events);
}

static void test_downstream_failures_are_counted_independently(void) {
    voice_wake_event_t event = {0};
    wake_feedback_snapshot_t before;
    wake_feedback_snapshot_t after;

    wake_feedback_host_set_cue_result(PROMPT_TONE_ERR_QUEUE);
    wake_feedback_host_set_led_result(LED_INDICATOR_ERR_NOT_INITIALIZED);
    wake_feedback_host_set_telemetry_result(ESP_FAIL);

    before = wake_feedback_get_snapshot();
    strncpy(event.detail_code, "wake_low_conf", sizeof(event.detail_code) - 1);
    wake_feedback_host_emit_wake(&event);

    after = wake_feedback_get_snapshot();
    /* The wake is still accepted; each failing consumer has its own counter. */
    assert(after.accepted_events == before.accepted_events + 1);
    assert(after.cue_failures == before.cue_failures + 1);
    assert(after.led_failures == before.led_failures + 1);
    assert(after.telemetry_failures == before.telemetry_failures + 1);
}

static void test_error_names_are_stable(void) {
    assert(strcmp(wake_feedback_error_name(WAKE_FEEDBACK_OK), "ok") == 0);
    assert(
        strcmp(
            wake_feedback_error_name(WAKE_FEEDBACK_ERR_NOT_INITIALIZED),
            "wake_feedback_not_initialized"
        ) == 0
    );
    assert(
        strcmp(
            wake_feedback_error_name(WAKE_FEEDBACK_ERR_NOT_READY),
            "wake_feedback_not_ready"
        ) == 0
    );
}

static void test_module_descriptor_is_stable(void) {
    const module_descriptor_t *descriptor = wake_feedback_module_descriptor();
    assert(descriptor != NULL);
    assert(strcmp(descriptor->module_name, "wake_feedback") == 0);
    assert(strcmp(descriptor->version, "1.0.0") == 0);
    assert(descriptor->initialize != NULL);
    assert(descriptor->shutdown != NULL);
}

int main(void) {
    test_init_requires_a_ready_detector();
    test_init_registers_once();
    test_accepted_wake_fans_out_to_all_consumers();
    test_empty_detail_code_uses_unknown_fallback();
    test_null_event_is_rejected();
    test_downstream_failures_are_counted_independently();
    test_error_names_are_stable();
    test_module_descriptor_is_stable();
    return 0;
}
