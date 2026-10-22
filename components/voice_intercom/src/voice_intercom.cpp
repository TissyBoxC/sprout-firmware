#include "voice_intercom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ArduinoJson.h"

#include "audio_input.h"
#include "audio_output.h"
#include "audio_pipeline.h"
#include "config_store.h"
#include "device_identity.h"
#include "device_message.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_indicator.h"
#include "mqtt_client.h"
#include "network_manager.h"
#include "prompt_tone.h"
#include "sdkconfig.h"
#include "voice_session.h"

#include "voice_intercom_core.h"

#if CONFIG_FEATURE_PRIVACY_GUARD && __has_include("privacy_guard.h")
#include "privacy_guard.h"
#define VOICE_INTERCOM_HAS_PRIVACY_GUARD 1
#else
#define VOICE_INTERCOM_HAS_PRIVACY_GUARD 0
#endif

// Kconfig defaults mirror the fallbacks in the sibling feature modules so a
// host test build without a generated sdkconfig still compiles.
#ifndef CONFIG_VOICE_INTERCOM_RING_TIMEOUT_SECONDS
#define CONFIG_VOICE_INTERCOM_RING_TIMEOUT_SECONDS 30
#endif
#ifndef CONFIG_VOICE_INTERCOM_MAX_CALL_SECONDS
#define CONFIG_VOICE_INTERCOM_MAX_CALL_SECONDS 300
#endif

// The same configuration keys the runtime reporter uses for its mutual-TLS
// command channel. Only an mqtts:// URL is accepted, so certificate and
// hostname validation stay enabled.
#define VOICE_INTERCOM_MQTT_URL_KEY "mqtt_url"
#define VOICE_INTERCOM_MQTT_CA_KEY "mqtt_ca"
#define VOICE_INTERCOM_MQTT_CERT_KEY "mqtt_cert"
#define VOICE_INTERCOM_MQTT_KEY_KEY "mqtt_key"
#define VOICE_INTERCOM_MQTT_URL_SIZE 192
#define VOICE_INTERCOM_MQTT_PEM_SIZE 6144
#define VOICE_INTERCOM_COMMAND_TOPIC_SIZE 128
#define VOICE_INTERCOM_ACK_TOPIC_SIZE 128
#define VOICE_INTERCOM_ENVELOPE_SIZE 1024

// One command is bounded by the shared device command contract; the envelope
// bound below keeps a hostile broker payload from forcing a larger buffer.
#define VOICE_INTERCOM_COMMAND_SIZE 2048

// The call `data` object is smaller than the envelope it arrives in, but it
// is re-serialized before parsing, so it needs its own bound.
#define VOICE_INTERCOM_CALL_DATA_SIZE 512

#define VOICE_INTERCOM_TASK_STACK_SIZE 3072
#define VOICE_INTERCOM_RING_PROMPT_INTERVAL_MS 2000u

static const char *const TAG = "voice_intercom";

// The shared header is plain C, so every module symbol keeps C linkage while
// this file uses C++ only for the bounded JSON helpers.
extern "C" {

static SemaphoreHandle_t s_lock;
static voice_intercom_state_t s_state;
static bool s_initialized;
static char s_caller_name[VOICE_INTERCOM_CALLER_NAME_SIZE];
static uint32_t s_calls_answered;
static uint32_t s_calls_declined;
static uint32_t s_calls_ended_by_guardian;
static uint32_t s_ring_timeouts;
static uint32_t s_duration_limits;
static uint32_t s_connect_failures;
static uint32_t s_commands_rejected;
static uint32_t s_ring_prompt_count;
static uint32_t s_last_ring_prompt_ms;
static TaskHandle_t s_ring_task;
static volatile bool s_ring_task_stop;

// The signalling client only lives while a call phase needs it, so an idle
// device holds no broker connection of its own.
static esp_mqtt_client_handle_t s_mqtt_client;
static bool s_signalling_subscribed;
static bool s_media_started;
static char s_device_id[DEVICE_IDENTIFIER_SIZE];
static char s_command_topic[VOICE_INTERCOM_COMMAND_TOPIC_SIZE];
static char s_ack_topic[VOICE_INTERCOM_ACK_TOPIC_SIZE];
static char *s_mqtt_ca_pem;
static char *s_mqtt_cert_pem;
static char *s_mqtt_key_pem;

static esp_err_t voice_intercom_apply_command(
    const char *payload,
    size_t payload_length,
    bool allow_ring
);

/** @brief FreeRTOS entry point for the repeating ring prompt. */
static void voice_intercom_ring_task(void *argument);

/** @brief Ring prompt loop, kept separate from the C task signature. */
static void voice_intercom_ring_task_body(void);

static void voice_intercom_lock(void) {
    if (s_lock != NULL) {
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void voice_intercom_unlock(void) {
    if (s_lock != NULL) {
        (void)xSemaphoreGive(s_lock);
    }
}

static bool voice_intercom_ensure_lock(void) {
    if (s_lock != NULL) {
        return true;
    }
    s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL;
}

static uint32_t voice_intercom_now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static uint32_t voice_intercom_hash_call_id(const char *call_id) {
    uint32_t hash = 2166136261u;
    for (const char *cursor = call_id; cursor != NULL && *cursor != '\0';
         ++cursor) {
        hash ^= (uint32_t)(unsigned char)*cursor;
        hash *= 16777619u;
    }
    return hash;
}

static void voice_intercom_copy_text(
    char *destination,
    size_t destination_size,
    const char *source
) {
    if (destination == NULL || destination_size == 0) {
        return;
    }
    const size_t length = source == NULL ? 0 : strlen(source);
    const size_t bounded = length < destination_size - 1
        ? length
        : destination_size - 1;
    if (bounded > 0) {
        memcpy(destination, source, bounded);
    }
    destination[bounded] = '\0';
}

// ---------------------------------------------------------------- indication

static void voice_intercom_publish_indication(void) {
    voice_intercom_lock();
    const voice_intercom_phase_t phase = s_state.phase;
    voice_intercom_unlock();
#if CONFIG_FEATURE_LED_INDICATOR
    switch (phase) {
        case VOICE_INTERCOM_PHASE_RINGING:
            (void)led_indicator_set_state(LED_INDICATOR_STATE_LISTENING);
            break;
        case VOICE_INTERCOM_PHASE_CONNECTING:
            (void)led_indicator_set_state(LED_INDICATOR_STATE_CONNECTING);
            break;
        case VOICE_INTERCOM_PHASE_ACTIVE:
            (void)led_indicator_set_state(LED_INDICATOR_STATE_SPEAKING);
            break;
        case VOICE_INTERCOM_PHASE_IDLE:
        default:
            (void)led_indicator_set_state(LED_INDICATOR_STATE_IDLE);
            break;
    }
#endif
}

static void voice_intercom_play_ring_prompt(void) {
#if CONFIG_FEATURE_PROMPT_TONE
    (void)prompt_tone_play(PROMPT_TONE_NETWORK_RESTORED);
#elif CONFIG_FEATURE_AUDIO_OUTPUT
    // Without the prompt generator the ring still has to be audible; a silent
    // frame keeps the mixer pacing without adding noise.
    audio_codec_pcm_frame_t silence = {};
    silence.pcm_size = AUDIO_CODEC_FRAME_PCM_BYTES;
    (void)audio_output_submit(&silence, AUDIO_OUTPUT_PRIORITY_PROMPT);
#endif
}

// The repeating ring never runs while the phase is anything but RINGING, so
// an accept, decline or timeout silences it on the next iteration.
static void voice_intercom_ring_prompt_step(void) {
    bool should_prompt = false;
    voice_intercom_lock();
    if (s_state.phase == VOICE_INTERCOM_PHASE_RINGING &&
        s_ring_prompt_count < VOICE_INTERCOM_MAX_RING_PROMPTS) {
        const uint32_t now_ms = voice_intercom_now_ms();
        // A prompt every interval is audible without becoming an alarm, and
        // the count bound stops a runaway loop if the guardian keeps
        // re-inviting the same device.
        if (s_ring_prompt_count == 0 ||
            (uint32_t)(now_ms - s_last_ring_prompt_ms) >=
                VOICE_INTERCOM_RING_PROMPT_INTERVAL_MS) {
            s_ring_prompt_count += 1;
            s_last_ring_prompt_ms = now_ms;
            should_prompt = true;
        }
    }
    voice_intercom_unlock();
    if (should_prompt) {
        voice_intercom_play_ring_prompt();
    }
}

// The FreeRTOS task entry is a plain C function pointer, so the body waits
// here instead of inside the callback type the ESP-IDF header declares.
static void voice_intercom_ring_task_body(void) {
    while (!s_ring_task_stop) {
        voice_intercom_ring_prompt_step();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    s_ring_task = NULL;
    vTaskDelete(NULL);
}

static void voice_intercom_show_ring(const char *caller_name) {
    voice_intercom_lock();
    s_ring_prompt_count = 0;
    s_last_ring_prompt_ms = 0;
    voice_intercom_copy_text(
        s_caller_name,
        sizeof(s_caller_name),
        caller_name
    );
    char display_name[VOICE_INTERCOM_CALLER_NAME_SIZE] = {};
    voice_intercom_copy_text(
        display_name,
        sizeof(display_name),
        caller_name
    );
    voice_intercom_unlock();
#if CONFIG_FEATURE_DEVICE_MESSAGE
    // The screen shows who is calling and how to answer. Only the guardian
    // supplied display name is rendered; no audio or call content ever
    // reaches the display.
    device_message_t message = {};
    snprintf(message.id, sizeof(message.id), "voice_intercom_ring");
    snprintf(message.title, sizeof(message.title), "家长来电");
    snprintf(
        message.body,
        sizeof(message.body),
        "%s 想和你通话\n短按按钮接听，长按按钮拒绝",
        display_name[0] != '\0' ? display_name : "家长"
    );
    snprintf(message.severity, sizeof(message.severity), "info");
    snprintf(message.category, sizeof(message.category), "voice_intercom");
    message.duration_seconds = CONFIG_VOICE_INTERCOM_RING_TIMEOUT_SECONDS;
    (void)device_message_present(&message, NULL);
#endif
    s_ring_task_stop = false;
    // The first prompt is synchronous so the ring is audible the moment the
    // screen appears, even when the repeat task cannot be created.
    voice_intercom_play_ring_prompt();
    voice_intercom_lock();
    s_ring_prompt_count = 1;
    s_last_ring_prompt_ms = voice_intercom_now_ms();
    voice_intercom_unlock();
    if (s_ring_task == NULL &&
        xTaskCreate(
            voice_intercom_ring_task,
            "voice_intercom_ring",
            VOICE_INTERCOM_TASK_STACK_SIZE,
            NULL,
            2,
            &s_ring_task
        ) != pdPASS) {
        s_ring_task = NULL;
        // The LED, screen and the first prompt still announce the call, so a
        // missing repeat task does not make the call silent.
        ESP_LOGW(TAG, "ring prompt task unavailable");
    }
    voice_intercom_publish_indication();
}

static void voice_intercom_ring_task(void *argument) {
    (void)argument;
    voice_intercom_ring_task_body();
}

static void voice_intercom_clear_indication(void) {
    s_ring_task_stop = true;
    voice_intercom_lock();
    s_ring_prompt_count = 0;
    s_caller_name[0] = '\0';
    voice_intercom_unlock();
#if CONFIG_FEATURE_LED_INDICATOR
    (void)led_indicator_set_state(LED_INDICATOR_STATE_IDLE);
#endif
}

// ---------------------------------------------------------------- signalling

static void voice_intercom_clear_secrets(void) {
    if (s_mqtt_key_pem != NULL) {
        memset(s_mqtt_key_pem, 0, strlen(s_mqtt_key_pem));
        free(s_mqtt_key_pem);
        s_mqtt_key_pem = NULL;
    }
    if (s_mqtt_cert_pem != NULL) {
        free(s_mqtt_cert_pem);
        s_mqtt_cert_pem = NULL;
    }
    if (s_mqtt_ca_pem != NULL) {
        free(s_mqtt_ca_pem);
        s_mqtt_ca_pem = NULL;
    }
}

static esp_err_t voice_intercom_read_pem(const char *key, char **pem_out) {
    char *buffer = (char *)calloc(1, VOICE_INTERCOM_MQTT_PEM_SIZE);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t result = config_store_get_string(
        key,
        buffer,
        VOICE_INTERCOM_MQTT_PEM_SIZE
    );
    if (result != ESP_OK) {
        memset(buffer, 0, VOICE_INTERCOM_MQTT_PEM_SIZE);
        free(buffer);
        return result;
    }
    *pem_out = buffer;
    return ESP_OK;
}

static void voice_intercom_publish_ack(
    const char *call_id,
    const char *status,
    const char *result_code
) {
    // Stop publishes once the broker is closed so a queued acknowledgement
    // never outlives the credential that was cleared with it.
    if (s_mqtt_client == NULL || s_device_id[0] == '\0' ||
        s_ack_topic[0] == '\0' || call_id == NULL) {
        return;
    }
    JsonDocument document;
    document["schema_version"] = "1.0.0";
    document["event_type"] = "device.command_ack";
    document["device_id"] = s_device_id;
    // The platform derives the command id from the call id, so a stable hash
    // lets the guardian match the acknowledgement to its invitation without
    // the device ever logging the identifier.
    char command_id[64] = {};
    snprintf(
        command_id,
        sizeof(command_id),
        "voice_intercom-%08lx",
        (unsigned long)voice_intercom_hash_call_id(call_id)
    );
    document["command_id"] = command_id;
    document["status"] = status;
    document["result_code"] = result_code == NULL ? "ok" : result_code;
    char payload[VOICE_INTERCOM_ENVELOPE_SIZE] = {};
    const size_t length = serializeJson(
        document,
        payload,
        sizeof(payload)
    );
    // A full buffer means the envelope was truncated, so it is never sent.
    if (length > 0 && length < sizeof(payload) - 1) {
        (void)esp_mqtt_client_publish(
            s_mqtt_client,
            s_ack_topic,
            payload,
            (int)length,
            1,
            0
        );
    }
    memset(payload, 0, sizeof(payload));
}

static bool voice_intercom_subscribe(void) {
    if (s_mqtt_client == NULL || s_command_topic[0] == '\0') {
        return false;
    }
    const int message_id = esp_mqtt_client_subscribe_single(
        s_mqtt_client,
        s_command_topic,
        1
    );
    // A negative identifier means the request was not queued, typically
    // because the socket is still connecting; the connect event retries.
    return message_id >= 0;
}

static void voice_intercom_mqtt_event(
    void *argument,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
) {
    (void)argument;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    if (event == NULL) {
        return;
    }
    if (event_id == MQTT_EVENT_CONNECTED) {
        s_signalling_subscribed = voice_intercom_subscribe();
        return;
    }
    if (event_id == MQTT_EVENT_DISCONNECTED) {
        s_signalling_subscribed = false;
        return;
    }
    if (event_id != MQTT_EVENT_DATA || event->data == NULL ||
        event->data_len <= 0) {
        return;
    }
    if (event->data_len > VOICE_INTERCOM_COMMAND_SIZE) {
        return;
    }
    char payload[VOICE_INTERCOM_COMMAND_SIZE] = {};
    memcpy(payload, event->data, (size_t)event->data_len);
    // Only a guardian hangup is applied inline. Starting a call from inside
    // the ESP-MQTT event task would block the socket on the TLS handshake.
    (void)voice_intercom_apply_command(payload, (size_t)event->data_len, false);
}

static esp_err_t voice_intercom_open_signalling(void) {
    if (s_mqtt_client != NULL) {
        return ESP_OK;
    }
    if (network_manager_get_state() != NETWORK_MANAGER_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device_identity_copy(s_device_id, sizeof(s_device_id)) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    char broker_url[VOICE_INTERCOM_MQTT_URL_SIZE] = {};
    if (config_store_get_string(
            VOICE_INTERCOM_MQTT_URL_KEY,
            broker_url,
            sizeof(broker_url)
        ) != ESP_OK) {
        memset(broker_url, 0, sizeof(broker_url));
        return ESP_ERR_NOT_FOUND;
    }
    if (strncmp(broker_url, "mqtts://", 8) != 0) {
        // A cleartext broker would expose call control to the local network;
        // the phase reports unavailable instead of silently downgrading.
        memset(broker_url, 0, sizeof(broker_url));
        ESP_LOGE(TAG, "signalling broker must use mqtts://");
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(
        s_command_topic,
        sizeof(s_command_topic),
        "sprout/v1/devices/%s/commands",
        s_device_id
    );
    snprintf(
        s_ack_topic,
        sizeof(s_ack_topic),
        "sprout/v1/devices/%s/acks",
        s_device_id
    );
    esp_err_t result = voice_intercom_read_pem(
        VOICE_INTERCOM_MQTT_CA_KEY,
        &s_mqtt_ca_pem
    );
    if (result == ESP_OK) {
        result = voice_intercom_read_pem(
            VOICE_INTERCOM_MQTT_CERT_KEY,
            &s_mqtt_cert_pem
        );
    }
    if (result == ESP_OK) {
        result = voice_intercom_read_pem(
            VOICE_INTERCOM_MQTT_KEY_KEY,
            &s_mqtt_key_pem
        );
    }
    if (result != ESP_OK) {
        memset(broker_url, 0, sizeof(broker_url));
        voice_intercom_clear_secrets();
        return result;
    }
    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = broker_url;
    config.broker.verification.certificate = s_mqtt_ca_pem;
    config.credentials.client_id = s_device_id;
    config.credentials.authentication.certificate = s_mqtt_cert_pem;
    config.credentials.authentication.key = s_mqtt_key_pem;
    s_mqtt_client = esp_mqtt_client_init(&config);
    memset(broker_url, 0, sizeof(broker_url));
    if (s_mqtt_client == NULL) {
        voice_intercom_clear_secrets();
        return ESP_ERR_NO_MEM;
    }
    (void)esp_mqtt_client_register_event(
        s_mqtt_client,
        MQTT_EVENT_ANY,
        voice_intercom_mqtt_event,
        NULL
    );
    s_signalling_subscribed = false;
    result = esp_mqtt_client_start(s_mqtt_client);
    if (result != ESP_OK) {
        (void)esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        voice_intercom_clear_secrets();
        return result;
    }
    // The connect event subscribes; this call covers a broker that delivers
    // the connect event before the event handler is registered.
    s_signalling_subscribed = voice_intercom_subscribe();
    return ESP_OK;
}

static void voice_intercom_close_signalling(void) {
    if (s_mqtt_client != NULL) {
        (void)esp_mqtt_client_stop(s_mqtt_client);
        (void)esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
    }
    s_signalling_subscribed = false;
    voice_intercom_clear_secrets();
    s_command_topic[0] = '\0';
    s_ack_topic[0] = '\0';
}

// -------------------------------------------------------------------- media

static bool voice_intercom_capture_is_enabled(void) {
    audio_input_snapshot_t input = {};
    if (audio_input_get_snapshot(&input) != ESP_OK) {
        return false;
    }
    return input.is_running;
}

static bool voice_intercom_privacy_allows_audio(void) {
#if VOICE_INTERCOM_HAS_PRIVACY_GUARD
    privacy_guard_decision_t decision = {};
    if (privacy_guard_evaluate(
            PRIVACY_GUARD_DATA_AUDIO_UPLOAD,
            &decision
        ) != PRIVACY_GUARD_OK) {
        return false;
    }
    return decision.allowed;
#else
    return true;
#endif
}

static esp_err_t voice_intercom_start_media(void) {
    if (!voice_intercom_privacy_allows_audio()) {
        // Without guardian consent there is no legitimate audio path, so the
        // microphone must not open at all.
        return ESP_ERR_INVALID_STATE;
    }
    if (!voice_session_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = voice_session_start_session(NULL, NULL);
    if (result != ESP_OK) {
        return result;
    }
    s_media_started = true;
    // voice_session_start_session holds its own capture lease; reading it back
    // proves the microphone is actually available before the call is ACTIVE.
    return voice_intercom_capture_is_enabled()
        ? ESP_OK
        : ESP_ERR_INVALID_STATE;
}

// Release every media resource. The session teardown stops capture and drops
// its lease, so the explicit input stop and lease release below only cover the
// path where the session failed halfway and left the microphone owned.
static void voice_intercom_release_media(void) {
    if (!s_media_started) {
        return;
    }
    s_media_started = false;
    voice_session_shutdown();
    (void)audio_input_stop();
    (void)audio_pipeline_capture_release(
        AUDIO_PIPELINE_CAPTURE_OWNER_AUDIO_INPUT
    );
#if CONFIG_FEATURE_AUDIO_OUTPUT
    // Drop queued conversation audio so the speaker goes quiet immediately
    // while the safety lane is preserved.
    (void)audio_output_flush(
        AUDIO_OUTPUT_PRIORITY_CONVERSATION,
        false,
        NULL
    );
#endif
}

// ------------------------------------------------------------- transitions

// Applies one state-machine outcome to every owned resource so no phase can
// keep audio or an indication alive.
static void voice_intercom_apply_end(voice_intercom_end_reason_t reason) {
    voice_intercom_clear_indication();
    voice_intercom_release_media();
    voice_intercom_lock();
    // The gate closes with the call, so the next invitation cannot be
    // accepted by a stale accept that never came from a button press.
    s_state.config.accept_enabled = false;
    char call_id[VOICE_INTERCOM_CALL_ID_SIZE] = {};
    memcpy(call_id, s_state.call_id, sizeof(call_id));
    switch (reason) {
        case VOICE_INTERCOM_END_RING_TIMEOUT:
            s_ring_timeouts += 1;
            break;
        case VOICE_INTERCOM_END_MAX_DURATION:
            s_duration_limits += 1;
            break;
        case VOICE_INTERCOM_END_CONNECT_FAILED:
            s_connect_failures += 1;
            break;
        case VOICE_INTERCOM_END_LOCAL_DECLINE:
            s_calls_declined += 1;
            break;
        case VOICE_INTERCOM_END_REMOTE_HANGUP:
            s_calls_ended_by_guardian += 1;
            break;
        default:
            break;
    }
    voice_intercom_unlock();
    if (s_mqtt_client != NULL) {
        voice_intercom_publish_ack(
            call_id,
            "failed",
            voice_intercom_end_reason_name(reason)
        );
    }
    // The acknowledgement must reach the broker before the client closes, so
    // the signalling path is torn down last.
    voice_intercom_close_signalling();
    voice_intercom_publish_indication();
    // Only the stable reason name is logged; the call id is an opaque platform
    // identifier and is deliberately kept out of the log sink.
    ESP_LOGI(TAG, "call ended: %s", voice_intercom_end_reason_name(reason));
}

static voice_intercom_snapshot_t voice_intercom_build_snapshot(void) {
    voice_intercom_snapshot_t snapshot = {};
    voice_intercom_lock();
    snapshot.phase = s_state.phase;
    snapshot.end_reason = s_state.end_reason;
    memcpy(snapshot.call_id, s_state.call_id, sizeof(snapshot.call_id));
    memcpy(
        snapshot.caller_name,
        s_caller_name,
        sizeof(snapshot.caller_name)
    );
    snapshot.audio_allowed = s_state.audio_allowed;
    snapshot.remaining_seconds = voice_intercom_core_remaining_seconds(
        &s_state,
        voice_intercom_now_ms()
    );
    snapshot.calls_answered = s_calls_answered;
    snapshot.calls_declined = s_calls_declined;
    snapshot.calls_ended_by_guardian = s_calls_ended_by_guardian;
    snapshot.ring_timeouts = s_ring_timeouts;
    snapshot.duration_limits = s_duration_limits;
    snapshot.connect_failures = s_connect_failures;
    snapshot.commands_rejected = s_commands_rejected;
    snapshot.calls_received = s_calls_answered + s_calls_declined +
        s_calls_ended_by_guardian + s_ring_timeouts + s_connect_failures +
        s_duration_limits;
    voice_intercom_unlock();
    return snapshot;
}

static void voice_intercom_record_rejection(void) {
    voice_intercom_lock();
    s_commands_rejected += 1;
    voice_intercom_unlock();
}

static void voice_intercom_start_ring(
    const char *call_id,
    const char *caller_name
) {
    voice_intercom_lock();
    const voice_intercom_result_t result = voice_intercom_core_ring(
        &s_state,
        call_id,
        voice_intercom_now_ms()
    );
    voice_intercom_unlock();
    if (result != VOICE_INTERCOM_RESULT_OK) {
        voice_intercom_record_rejection();
        ESP_LOGW(
            TAG,
            "call invitation rejected: %s",
            voice_intercom_result_name(result)
        );
        return;
    }
    voice_intercom_show_ring(caller_name);
    // A ringing phase needs its own signalling client to receive the
    // guardian's cancel; no audio path is opened here.
    (void)voice_intercom_open_signalling();
    ESP_LOGI(TAG, "call ringing");
}

static void voice_intercom_accept_ringing_call(void) {
    voice_intercom_lock();
    // Opening the gate here is what makes the physical accept mandatory: this
    // function is only reachable from the button handler and the public
    // accept entry point, never from a guardian command.
    s_state.config.accept_enabled =
        s_state.phase == VOICE_INTERCOM_PHASE_RINGING;
    const bool is_ringing = s_state.phase == VOICE_INTERCOM_PHASE_RINGING;
    char call_id[VOICE_INTERCOM_CALL_ID_SIZE] = {};
    memcpy(call_id, s_state.call_id, sizeof(call_id));
    voice_intercom_unlock();
    if (!is_ringing) {
        return;
    }
    // The signalling connection opens before the realtime session so a failed
    // connect is reported to the guardian instead of a silent call.
    const esp_err_t signalling_result = voice_intercom_open_signalling();
    const esp_err_t media_result = signalling_result == ESP_OK
        ? voice_intercom_start_media()
        : signalling_result;
    const bool has_resources = voice_intercom_audio_resources_held(
        s_mqtt_client != NULL,
        media_result == ESP_OK && voice_intercom_capture_is_enabled()
    );
    voice_intercom_lock();
    const voice_intercom_result_t accept_result = voice_intercom_core_accept(
        &s_state,
        voice_intercom_now_ms(),
        has_resources
    );
    const bool is_connecting =
        s_state.phase == VOICE_INTERCOM_PHASE_CONNECTING;
    if (accept_result == VOICE_INTERCOM_RESULT_OK && is_connecting) {
        (void)voice_intercom_core_connected(&s_state, voice_intercom_now_ms());
        s_calls_answered += 1;
    }
    voice_intercom_unlock();
    if (accept_result != VOICE_INTERCOM_RESULT_OK || !is_connecting) {
        voice_intercom_apply_end(VOICE_INTERCOM_END_CONNECT_FAILED);
        return;
    }
    voice_intercom_publish_ack(call_id, "acknowledged", "accepted");
    voice_intercom_publish_indication();
    ESP_LOGI(TAG, "call active");
}

static void voice_intercom_end_call(voice_intercom_end_reason_t reason) {
    voice_intercom_lock();
    const voice_intercom_result_t ended = voice_intercom_core_end(
        &s_state,
        reason,
        voice_intercom_now_ms()
    );
    voice_intercom_unlock();
    // Only a call the state machine actually ended owns resources to release,
    // and the core refuses to end an idle call.
    if (ended == VOICE_INTERCOM_RESULT_OK) {
        voice_intercom_apply_end(reason);
    }
}

static void voice_intercom_tick(void) {
    voice_intercom_lock();
    const voice_intercom_end_reason_t reason = voice_intercom_core_tick(
        &s_state,
        voice_intercom_now_ms()
    );
    voice_intercom_unlock();
    if (reason != VOICE_INTERCOM_END_NONE) {
        voice_intercom_apply_end(reason);
    }
}
// Shared by the MQTT event task and the public entry point. The event task
// must never block on the TLS handshake, so it only forwards a hangup.
static esp_err_t voice_intercom_apply_command(
    const char *payload,
    size_t payload_length,
    bool allow_ring
) {
    JsonDocument document;
    if (deserializeJson(document, payload, payload_length) !=
        DeserializationError::Ok) {
        voice_intercom_record_rejection();
        return ESP_ERR_INVALID_ARG;
    }
    JsonObjectConst object = document.as<JsonObjectConst>();
    const char *command_type = object["command_type"] | "";
    const char *event_type = object["event_type"] | "";
    if (strcmp(event_type, "device.command") != 0) {
        return ESP_OK;
    }
    if (strcmp(command_type, "voice_intercom_call") != 0 &&
        strcmp(command_type, "voice_intercom_call_control") != 0) {
        return ESP_OK;
    }
    if (!allow_ring && strcmp(command_type, "voice_intercom_call") == 0) {
        // A call is only started from the public entry point so the TLS and
        // realtime setup never runs on the ESP-MQTT event task.
        return ESP_ERR_NOT_SUPPORTED;
    }
    // The verbatim object is re-serialized instead of read as a string,
    // because the envelope carries `data` as a nested JSON object.
    char data[VOICE_INTERCOM_CALL_DATA_SIZE] = {};
    const size_t data_length = serializeJson(
        object["data"],
        data,
        sizeof(data)
    );
    if (data_length == 0 || data_length >= sizeof(data) - 1) {
        memset(data, 0, sizeof(data));
        voice_intercom_record_rejection();
        return ESP_ERR_INVALID_ARG;
    }
    JsonDocument data_document;
    const DeserializationError data_result = deserializeJson(
        data_document,
        data,
        data_length
    );
    memset(data, 0, sizeof(data));
    if (data_result != DeserializationError::Ok) {
        voice_intercom_record_rejection();
        return ESP_ERR_INVALID_ARG;
    }
    const char *call_id = data_document["call_id"] | "";
    if (!voice_intercom_call_id_is_valid(call_id)) {
        voice_intercom_record_rejection();
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(command_type, "voice_intercom_call") == 0) {
        const char *caller_name = data_document["caller_name"] | "";
        voice_intercom_start_ring(call_id, caller_name);
        return ESP_OK;
    }
    // The control command can only end a call. Even an `accept` action from
    // the guardian is refused here: only the device-side physical press may
    // open the microphone.
    voice_intercom_lock();
    // The call identity is cleared when the call ends, so it is matched to the
    // live call here and the state machine below performs the actual end.
    const bool is_same_call =
        s_state.call_id[0] != '\0' && strcmp(s_state.call_id, call_id) == 0;
    voice_intercom_unlock();
    if (!is_same_call) {
        return ESP_OK;
    }
    const char *action = data_document["action"] | "";
    if (strcmp(action, "hangup") != 0 && strcmp(action, "decline") != 0) {
        return ESP_OK;
    }
    voice_intercom_end_call(VOICE_INTERCOM_END_REMOTE_HANGUP);
    return ESP_OK;
}

// ------------------------------------------------------------------- public

esp_err_t voice_intercom_init(void) {
    if (!voice_intercom_ensure_lock()) {
        return ESP_ERR_NO_MEM;
    }
    voice_intercom_lock();
    if (s_initialized) {
        voice_intercom_unlock();
        return ESP_OK;
    }
    const voice_intercom_config_t config = {
        .ring_timeout_ms =
            (uint32_t)CONFIG_VOICE_INTERCOM_RING_TIMEOUT_SECONDS * 1000u,
        .max_call_ms =
            (uint32_t)CONFIG_VOICE_INTERCOM_MAX_CALL_SECONDS * 1000u,
        .accept_enabled = false,
    };
    voice_intercom_core_init(&s_state, &config);
    s_initialized = true;
    voice_intercom_unlock();
    ESP_LOGI(TAG, "voice intercom ready");
    return ESP_OK;
}

bool voice_intercom_is_ready(void) {
    return s_initialized;
}

voice_intercom_phase_t voice_intercom_get_phase(void) {
    voice_intercom_lock();
    const voice_intercom_phase_t phase = s_state.phase;
    voice_intercom_unlock();
    return phase;
}

esp_err_t voice_intercom_get_snapshot(
    voice_intercom_snapshot_t *snapshot_out
) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *snapshot_out = voice_intercom_build_snapshot();
    return ESP_OK;
}

bool voice_intercom_is_audio_active(void) {
    voice_intercom_lock();
    const bool is_active =
        s_state.phase == VOICE_INTERCOM_PHASE_ACTIVE &&
        s_state.audio_allowed &&
        voice_intercom_capture_is_enabled();
    voice_intercom_unlock();
    return is_active;
}

esp_err_t voice_intercom_accept(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    voice_intercom_tick();
    if (voice_intercom_get_phase() != VOICE_INTERCOM_PHASE_RINGING) {
        return ESP_ERR_INVALID_STATE;
    }
    voice_intercom_accept_ringing_call();
    return voice_intercom_get_phase() == VOICE_INTERCOM_PHASE_ACTIVE
        ? ESP_OK
        : ESP_ERR_INVALID_STATE;
}

esp_err_t voice_intercom_decline(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    voice_intercom_tick();
    if (voice_intercom_get_phase() != VOICE_INTERCOM_PHASE_RINGING) {
        return ESP_ERR_INVALID_STATE;
    }
    voice_intercom_end_call(VOICE_INTERCOM_END_LOCAL_DECLINE);
    return ESP_OK;
}

esp_err_t voice_intercom_hangup(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    voice_intercom_tick();
    const voice_intercom_phase_t phase = voice_intercom_get_phase();
    if (phase == VOICE_INTERCOM_PHASE_IDLE) {
        // A stale button press or a repeated command must not fail a caller
        // that only learns the call is already over.
        return ESP_OK;
    }
    voice_intercom_end_call(
        phase == VOICE_INTERCOM_PHASE_RINGING
            ? VOICE_INTERCOM_END_LOCAL_DECLINE
            : VOICE_INTERCOM_END_LOCAL_HANGUP
    );
    return ESP_OK;
}

esp_err_t voice_intercom_handle_button(button_input_gesture_t gesture) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (gesture != BUTTON_INPUT_GESTURE_SHORT_PRESS &&
        gesture != BUTTON_INPUT_GESTURE_LONG_PRESS) {
        return ESP_OK;
    }
    voice_intercom_tick();
    switch (voice_intercom_get_phase()) {
        case VOICE_INTERCOM_PHASE_RINGING:
            // The short press is the child's accept. Nothing else on the
            // device can move a call out of the ringing phase.
            return gesture == BUTTON_INPUT_GESTURE_SHORT_PRESS
                ? voice_intercom_accept()
                : voice_intercom_decline();
        case VOICE_INTERCOM_PHASE_CONNECTING:
        case VOICE_INTERCOM_PHASE_ACTIVE:
            return voice_intercom_hangup();
        case VOICE_INTERCOM_PHASE_IDLE:
        default:
            return ESP_OK;
    }
}

esp_err_t voice_intercom_handle_command(
    const char *payload,
    size_t payload_length
) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (payload == NULL || payload_length == 0 ||
        payload_length > VOICE_INTERCOM_COMMAND_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    return voice_intercom_apply_command(payload, payload_length, true);
}

/**
 * @brief Advance both call timers and end the call when one expires.
 *
 * The composition root calls this from its periodic tick so the ring timeout
 * and the maximum call duration are enforced on the device even when no
 * network event or button press ever arrives.
 */
voice_intercom_end_reason_t voice_intercom_poll(void) {
    if (!s_initialized) {
        return VOICE_INTERCOM_END_NONE;
    }
    voice_intercom_lock();
    const voice_intercom_end_reason_t reason = voice_intercom_core_tick(
        &s_state,
        voice_intercom_now_ms()
    );
    voice_intercom_unlock();
    if (reason != VOICE_INTERCOM_END_NONE) {
        voice_intercom_apply_end(reason);
    }
    return reason;
}

const module_descriptor_t *voice_intercom_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "voice_intercom",
        .version = "1.0.0",
        .initialize = voice_intercom_init,
        .shutdown = NULL,
    };
    return &descriptor;
}

}  // extern "C"
