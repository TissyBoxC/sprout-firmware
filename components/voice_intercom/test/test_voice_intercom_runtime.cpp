// Host tests for the voice intercom runtime. Every dependency is a controlled
// double, so these cases prove the real transitions: no capture before the
// physical accept, full resource release on every end path, the on-device
// duration bound, and a guardian control command that can never accept.

#include <cstdio>
#include <cstring>

#include "audio_input.h"
#include "freertos/FreeRTOS.h"
#include "led_indicator.h"
#include "voice_intercom.h"
#include "voice_intercom_deps.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

const char *call_payload(const char *call_id, const char *caller_name) {
    static char payload[1024];
    std::snprintf(
        payload,
        sizeof(payload),
        "{\"schema_version\":\"1.0.0\",\"event_type\":\"device.command\","
        "\"device_id\":\"device-0001\",\"command_id\":\"cmd-%s\","
        "\"request_id\":\"req-1\",\"command_type\":\"voice_intercom_call\","
        "\"data\":{\"call_id\":\"%s\",\"caller_name\":\"%s\"}}",
        call_id,
        call_id,
        caller_name
    );
    return payload;
}

const char *control_payload(const char *call_id, const char *action) {
    static char payload[1024];
    std::snprintf(
        payload,
        sizeof(payload),
        "{\"schema_version\":\"1.0.0\",\"event_type\":\"device.command\","
        "\"device_id\":\"device-0001\",\"command_id\":\"ctl-%s\","
        "\"request_id\":\"req-2\","
        "\"command_type\":\"voice_intercom_call_control\","
        "\"data\":{\"call_id\":\"%s\",\"action\":\"%s\"}}",
        // The only format placeholder before `data` is the command id.
        call_id,
        call_id,
        action
    );
    return payload;
}

void reset_and_init(void) {
    // The module has no public reset and keeps its counters across calls, so
    // each case runs on a fresh process state by ending any previous call and
    // then re-baselining the snapshot it reads.
    (void)voice_intercom_poll();
    (void)voice_intercom_hangup();
    voice_intercom_host_reset();
    voice_intercom_host_set_broker_url("mqtts://broker.clarkhub.cn:8883");
    const esp_err_t result = voice_intercom_init();
    check(result == ESP_OK, "the module initializes");
}

void ring(void) {
    const char *payload = call_payload("call-1", "妈妈");
    check(
        voice_intercom_handle_command(payload, std::strlen(payload)) == ESP_OK,
        "a guardian call starts ringing"
    );
}

void test_ringing_captures_and_sends_nothing(void) {
    reset_and_init();
    ring();
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_RINGING,
        "the device rings after a guardian call"
    );
    check(
        voice_intercom_host_prompt_tone_calls() == 1,
        "the ring has one audible prompt"
    );
    check(
        voice_intercom_host_display_calls() == 1,
        "the screen shows the incoming call"
    );
    check(
        std::strstr(voice_intercom_host_display_body(), "妈妈") != nullptr,
        "the screen names the guardian"
    );
    check(
        std::strstr(voice_intercom_host_display_body(), "接听") != nullptr,
        "the screen explains how to accept"
    );
    check(
        voice_intercom_host_session_start_calls() == 0,
        "ringing starts no realtime session"
    );
    check(
        voice_intercom_host_capture_stop_calls() == 0,
        "ringing never touches the microphone"
    );
    check(!voice_intercom_is_audio_active(), "ringing is not an audio call");

    voice_intercom_snapshot_t snapshot = {};
    check(
        voice_intercom_get_snapshot(&snapshot) == ESP_OK,
        "the snapshot is readable while ringing"
    );
    check(
        snapshot.phase == VOICE_INTERCOM_PHASE_RINGING,
        "the snapshot reports ringing"
    );
    check(
        std::strcmp(snapshot.call_id, "call-1") == 0,
        "the snapshot carries the call identity"
    );
    check(snapshot.audio_allowed == false, "ringing holds no audio path");
}

void test_accept_starts_audio_only_after_the_button(void) {
    reset_and_init();
    ring();
    check(
        voice_intercom_handle_button(BUTTON_INPUT_GESTURE_DOUBLE_PRESS) ==
            ESP_OK,
        "an unrelated gesture is ignored"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_RINGING,
        "an unrelated gesture does not accept"
    );
    check(
        voice_intercom_host_session_start_calls() == 0,
        "no gesture other than the accept starts audio"
    );

    check(
        voice_intercom_handle_button(BUTTON_INPUT_GESTURE_SHORT_PRESS) ==
            ESP_OK,
        "a short press accepts the call"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_ACTIVE,
        "the accepted call is active"
    );
    check(
        voice_intercom_host_session_start_calls() == 1,
        "the realtime session starts once"
    );
    check(voice_intercom_is_audio_active(), "audio is active after accept");
    check(
        voice_intercom_host_mqtt_publish_calls() >= 1,
        "the signalling client publishes one acknowledgement"
    );
    check(
        voice_intercom_host_mqtt_subscribe_calls() >= 1,
        "the signalling client subscribes to the command topic"
    );
    check(
        std::strstr(
            voice_intercom_host_mqtt_published_payload(),
            "\"result_code\":\"accepted\""
        ) != nullptr,
        "the acknowledgement reports the accept"
    );
    check(
        std::strstr(
            voice_intercom_host_mqtt_published_topic(),
            "/commands/ack"
        ) != nullptr,
        "the acknowledgement goes to the device ack topic"
    );
    check(
        std::strstr(
            voice_intercom_host_mqtt_published_payload(),
            "妈妈"
        ) == nullptr,
        "the acknowledgement never carries personal data"
    );
}

void test_decline_releases_the_ring(void) {
    reset_and_init();
    ring();
    check(
        voice_intercom_handle_button(BUTTON_INPUT_GESTURE_LONG_PRESS) ==
            ESP_OK,
        "a long press declines"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
        "the declined call leaves the device idle"
    );
    check(
        voice_intercom_host_session_start_calls() == 0,
        "a declined call never starts audio"
    );
    check(
        voice_intercom_host_led_state() == LED_INDICATOR_STATE_IDLE,
        "the indicator is cleared on decline"
    );
    check(
        voice_intercom_host_mqtt_stop_calls() >= 1,
        "the signalling client closes on decline"
    );
    voice_intercom_snapshot_t snapshot = {};
    (void)voice_intercom_get_snapshot(&snapshot);
    check(
        snapshot.end_reason == VOICE_INTERCOM_END_LOCAL_DECLINE,
        "the decline is recorded as the end reason"
    );
    check(snapshot.calls_declined >= 1, "the decline is counted");
    check(
        snapshot.audio_allowed == false,
        "the snapshot reports no audio path"
    );
    // A refused ring must reach the platform as a decline. Without it the
    // guardian's screen keeps ringing a call the child already ended.
    check(
        esp_http_client_host_perform_count() == 1,
        "a decline reports exactly one call state to the platform"
    );
    const char *decline_url = esp_http_client_host_perform_url(0);
    check(
        std::strstr(decline_url, "/intercom-calls/call-1/decline") != nullptr,
        "the refusal is reported on the decline route"
    );
    check(
        std::strstr(decline_url, "https://") == decline_url,
        "the report uses the platform endpoint over TLS"
    );
    check(
        std::strstr(
            esp_http_client_host_perform_body(0),
            "local_decline"
        ) != nullptr,
        "the report names the reason the call ended"
    );
    check(
        std::strstr(esp_http_client_host_perform_body(0), "audio") == nullptr,
        "the report carries no call audio"
    );
}

void test_ring_timeout_ends_the_call_on_device(void) {
    reset_and_init();
    ring();
    // The Kconfig default ring window is 30 seconds.
    voice_intercom_host_advance_ticks(29999);
    check(
        voice_intercom_poll() == VOICE_INTERCOM_END_NONE,
        "the ring continues inside the window"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_RINGING,
        "the device still rings inside the window"
    );
    voice_intercom_host_advance_ticks(1);
    check(
        voice_intercom_poll() == VOICE_INTERCOM_END_RING_TIMEOUT,
        "the ring timeout ends the call without any network event"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
        "the timed out call is released"
    );
    voice_intercom_snapshot_t snapshot = {};
    (void)voice_intercom_get_snapshot(&snapshot);
    check(snapshot.ring_timeouts == 1, "the ring timeout is counted");
    check(
        voice_intercom_host_session_start_calls() == 0,
        "a timed out call never opened the microphone"
    );
}

void test_hangup_releases_every_resource(void) {
    reset_and_init();
    ring();
    (void)voice_intercom_accept();
    check(voice_intercom_is_audio_active(), "the call carries audio");
    check(
        voice_intercom_hangup() == ESP_OK,
        "a hangup ends the active call"
    );
    check(!voice_intercom_is_audio_active(), "the hangup releases audio");
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
        "the device returns to idle"
    );
    check(
        voice_intercom_host_session_shutdown_calls() == 1,
        "the session is torn down on hangup"
    );
    check(
        voice_intercom_host_capture_stop_calls() >= 1,
        "the microphone is stopped on hangup"
    );
    check(
        voice_intercom_host_capture_lease_release_calls() >= 1,
        "the capture lease is released on hangup"
    );
    check(
        voice_intercom_host_output_flush_calls() >= 1,
        "conversation playback is flushed on hangup"
    );
    check(
        voice_intercom_host_led_state() == LED_INDICATOR_STATE_IDLE,
        "the indicator is cleared on hangup"
    );
    check(
        voice_intercom_host_mqtt_stop_calls() == 1 &&
            voice_intercom_host_mqtt_destroy_calls() == 1,
        "the signalling client is closed on hangup"
    );
    check(
        voice_intercom_hangup() == ESP_OK,
        "a repeated hangup is harmless"
    );
    check(
        voice_intercom_host_session_shutdown_calls() == 1,
        "a repeated hangup does not tear down twice"
    );
    // The accepted call reports twice: once when it connected and once when it
    // ended. A hangup sends the platform to its hangup route, never to decline,
    // so a guardian is not told the child refused a call they answered.
    check(
        esp_http_client_host_perform_count() == 2,
        "an answered call reports its accept and its hangup"
    );
    check(
        std::strstr(
            esp_http_client_host_perform_url(0),
            "/intercom-calls/call-1/accept"
        ) != nullptr,
        "the answered call is reported on the accept route"
    );
    check(
        std::strstr(
            esp_http_client_host_perform_url(1),
            "/intercom-calls/call-1/hangup"
        ) != nullptr,
        "the ended call is reported on the hangup route"
    );
    check(
        std::strstr(esp_http_client_host_perform_url(1), "decline") == nullptr,
        "an answered call is never reported as a decline"
    );
}

void test_max_duration_is_enforced_on_device(void) {
    reset_and_init();
    ring();
    (void)voice_intercom_accept();
    // The Kconfig default maximum call duration is 5 minutes.
    voice_intercom_host_advance_ticks(300000 - 1);
    check(
        voice_intercom_poll() == VOICE_INTERCOM_END_NONE,
        "the call continues below the duration limit"
    );
    voice_intercom_host_advance_ticks(1);
    check(
        voice_intercom_poll() == VOICE_INTERCOM_END_MAX_DURATION,
        "the device ends the call at the duration limit"
    );
    check(!voice_intercom_is_audio_active(), "the limit releases audio");
    check(
        voice_intercom_host_session_shutdown_calls() == 1,
        "the limit tears the session down"
    );
    voice_intercom_snapshot_t snapshot = {};
    (void)voice_intercom_get_snapshot(&snapshot);
    check(snapshot.duration_limits == 1, "the limit is counted");
}

void test_failed_session_start_never_becomes_active(void) {
    reset_and_init();
    ring();
    voice_intercom_host_set_session_start_result(ESP_ERR_INVALID_STATE);
    check(
        voice_intercom_accept() != ESP_OK,
        "a failed session start fails the accept"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
        "a failed session start releases the call"
    );
    check(!voice_intercom_is_audio_active(), "no audio after a failed start");
    voice_intercom_snapshot_t snapshot = {};
    (void)voice_intercom_get_snapshot(&snapshot);
    check(
        snapshot.end_reason == VOICE_INTERCOM_END_CONNECT_FAILED,
        "the failure is reported as connect_failed"
    );
    check(snapshot.connect_failures == 1, "the failure is counted");
}

void test_missing_capture_lease_never_becomes_active(void) {
    reset_and_init();
    ring();
    voice_intercom_host_set_capture_missing();
    check(
        voice_intercom_accept() != ESP_OK,
        "an accept without the capture lease fails"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
        "a call without the capture lease is released"
    );
    check(!voice_intercom_is_audio_active(), "no audio without the lease");
}

void test_guardian_control_can_never_accept(void) {
    reset_and_init();
    ring();
    const char *payload = control_payload("call-1", "accept");
    check(
        voice_intercom_handle_command(payload, std::strlen(payload)) == ESP_OK,
        "an accept control command is answered without error"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_RINGING,
        "a guardian command cannot accept the call"
    );
    check(
        voice_intercom_host_session_start_calls() == 0,
        "a guardian command never opens the microphone"
    );
}

void test_guardian_hangup_ends_ringing_and_active_calls(void) {
    reset_and_init();
    ring();
    const char *hangup = control_payload("call-1", "hangup");
    check(
        voice_intercom_handle_command(hangup, std::strlen(hangup)) == ESP_OK,
        "the guardian can cancel a ringing call"
    );
    voice_intercom_snapshot_t snapshot = {};
    (void)voice_intercom_get_snapshot(&snapshot);
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
        "the cancelled call is released"
    );
    check(
        snapshot.end_reason == VOICE_INTERCOM_END_REMOTE_HANGUP,
        "the guardian cancel is recorded"
    );
    check(
        snapshot.calls_ended_by_guardian == 1,
        "the guardian cancel is counted"
    );

    reset_and_init();
    ring();
    (void)voice_intercom_accept();
    check(voice_intercom_is_audio_active(), "the second call is active");
    check(
        voice_intercom_handle_command(hangup, std::strlen(hangup)) == ESP_OK,
        "the guardian can end an active call"
    );
    check(!voice_intercom_is_audio_active(), "the guardian hangup frees audio");
    check(
        voice_intercom_host_session_shutdown_calls() == 1,
        "the guardian hangup tears the session down"
    );
}

void test_mqtt_delivery_path_applies_a_hangup(void) {
    reset_and_init();
    (void)voice_intercom_accept();
    const char *hangup = control_payload("call-1", "hangup");
    voice_intercom_host_deliver_mqtt_data(hangup, (int)std::strlen(hangup));
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
        "a delivered guardian hangup ends the call"
    );
    check(!voice_intercom_is_audio_active(), "the delivered hangup frees audio");
}

void test_untrusted_inputs_are_rejected(void) {
    reset_and_init();
    const char *bad_payloads[] = {
        "not json",
        "{\"event_type\":\"device.command\","
        "\"command_type\":\"voice_intercom_call\",\"data\":{\"call_id\":\"\"}}",
        "{\"event_type\":\"device.command\","
        "\"command_type\":\"voice_intercom_call\","
        "\"data\":{\"call_id\":\"bad id\"}}",
    };
    for (size_t index = 0;
         index < sizeof(bad_payloads) / sizeof(bad_payloads[0]);
         ++index) {
        const esp_err_t result = voice_intercom_handle_command(
            bad_payloads[index],
            std::strlen(bad_payloads[index])
        );
        check(result != ESP_OK, "an unusable command is refused");
        check(
            voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
            "an unusable command starts no call"
        );
    }
    // Another event type belongs to a different subscriber and must be left
    // alone rather than counted as a rejection.
    const char *foreign = "{\"event_type\":\"other.event\"}";
    check(
        voice_intercom_handle_command(foreign, std::strlen(foreign)) == ESP_OK,
        "a command for another event type is ignored"
    );
    check(
        voice_intercom_handle_command(nullptr, 0) == ESP_ERR_INVALID_ARG,
        "a missing payload is refused"
    );
    check(
        voice_intercom_host_session_start_calls() == 0,
        "no unusable command reached the microphone"
    );
}

void test_cleartext_broker_is_refused(void) {
    voice_intercom_host_reset();
    voice_intercom_host_set_broker_url("mqtt://broker.clarkhub.cn:1883");
    (void)voice_intercom_init();
    (void)voice_intercom_poll();
    (void)voice_intercom_hangup();
    ring();
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_RINGING,
        "the device still rings for the child"
    );
    check(
        voice_intercom_accept() != ESP_OK,
        "accept without a TLS signalling channel fails"
    );
    check(
        voice_intercom_host_session_start_calls() == 0,
        "a cleartext signalling channel never opens the microphone"
    );
    check(
        voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_IDLE,
        "the call is released instead of downgrading"
    );
}

void test_unprovisioned_broker_is_refused(void) {
    voice_intercom_host_reset();
    (void)voice_intercom_init();
    (void)voice_intercom_poll();
    (void)voice_intercom_hangup();
    ring();
    check(
        voice_intercom_accept() != ESP_OK,
        "accept without broker configuration fails"
    );
    check(
        voice_intercom_host_session_start_calls() == 0,
        "an unprovisioned channel never opens the microphone"
    );
}

void test_offline_device_never_opens_audio(void) {
    voice_intercom_host_reset();
    voice_intercom_host_set_broker_url("mqtts://broker.clarkhub.cn:8883");
    voice_intercom_host_set_network_connected(false);
    (void)voice_intercom_init();
    (void)voice_intercom_poll();
    (void)voice_intercom_hangup();
    ring();
    check(
        voice_intercom_accept() != ESP_OK,
        "an offline accept fails"
    );
    check(
        voice_intercom_host_session_start_calls() == 0,
        "an offline device never opens the microphone"
    );
}

void test_snapshot_after_hangup_is_bounded(void) {
    reset_and_init();
    ring();
    (void)voice_intercom_accept();
    (void)voice_intercom_hangup();
    voice_intercom_snapshot_t snapshot = {};
    check(
        voice_intercom_get_snapshot(&snapshot) == ESP_OK,
        "the snapshot is readable after a call"
    );
    check(snapshot.calls_answered >= 1, "the answered call is counted");
    check(snapshot.calls_received >= 1, "the call was received");
    check(
        snapshot.end_reason == VOICE_INTERCOM_END_LOCAL_HANGUP,
        "the local hangup is recorded"
    );
    check(snapshot.remaining_seconds == 0, "no timer remains armed");
    check(snapshot.audio_allowed == false, "no audio remains allowed");
    check(
        voice_intercom_get_snapshot(nullptr) == ESP_ERR_INVALID_ARG,
        "the snapshot refuses a missing output"
    );
}

}  // namespace

int main(void) {
    test_ringing_captures_and_sends_nothing();
    test_accept_starts_audio_only_after_the_button();
    test_decline_releases_the_ring();
    test_ring_timeout_ends_the_call_on_device();
    test_hangup_releases_every_resource();
    test_max_duration_is_enforced_on_device();
    test_failed_session_start_never_becomes_active();
    test_missing_capture_lease_never_becomes_active();
    test_guardian_control_can_never_accept();
    test_guardian_hangup_ends_ringing_and_active_calls();
    test_mqtt_delivery_path_applies_a_hangup();
    test_untrusted_inputs_are_rejected();
    test_cleartext_broker_is_refused();
    test_unprovisioned_broker_is_refused();
    test_offline_device_never_opens_audio();
    test_snapshot_after_hangup_is_bounded();
    if (failures != 0) {
        std::printf("%d runtime assertions failed\n", failures);
        return 1;
    }
    std::printf("voice_intercom_runtime: all assertions passed\n");
    return 0;
}
