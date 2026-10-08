#include "voice_session.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "audio_input.h"
#include "audio_pipeline.h"
#include "cloud_auth.h"
#include "cJSON.h"
#include "device_binding_client.h"
#include "device_identity.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "playback_queue.h"
#include "sdkconfig.h"
#include "voice_session_protocol.h"

#if CONFIG_FEATURE_DEVICE_CAPABILITIES
#include "device_capabilities.h"
#endif
#if CONFIG_FEATURE_VERSION_INFO
#include "version_info.h"
#endif
#if CONFIG_FEATURE_VOICE_WAKE
#include "voice_wake.h"
#endif
#if CONFIG_FEATURE_CONVERSATION_CONTEXT
#include "conversation_context.h"
#endif
#if CONFIG_FEATURE_CHILD_PROMPT_PROFILE
#include "child_prompt_profile.h"
#endif
#if CONFIG_FEATURE_PARENT_CONTROL_RUNTIME && __has_include("parent_control_runtime.h")
#include "parent_control_runtime.h"
#include "time_sync.h"
#include "usage_ledger.h"
#define VOICE_SESSION_HAS_PARENT_CONTROL_RUNTIME 1
#else
#define VOICE_SESSION_HAS_PARENT_CONTROL_RUNTIME 0
#endif

static const char *const TAG = "voice_session";

// ---------------------------------------------------------------------------
// Bounded configuration and buffers
// ---------------------------------------------------------------------------

/** @brief Control frames waiting for the sender task. */
#define VOICE_SESSION_CONTROL_QUEUE_DEPTH 8

/** @brief Outbound audio frames waiting for the sender task. */
#define VOICE_SESSION_AUDIO_QUEUE_DEPTH 16

/** @brief Longest control frame accepted or emitted, including terminator. */
#define VOICE_SESSION_CONTROL_FRAME_SIZE 1024

/**
 * @brief Websocket receive buffer large enough for one full audio envelope.
 *
 * An SRSV frame is at most 24 header bytes + 127 identifier bytes + 1024
 * payload bytes, so a smaller buffer would split the frame across events.
 */
#define VOICE_SESSION_WS_BUFFER_SIZE 1600

/** @brief Bounded time-formatting buffer for RFC3339 sent_at. */
#define VOICE_SESSION_TIMESTAMP_SIZE 32

/** @brief Sender task stack and priority. */
#define VOICE_SESSION_TASK_STACK_SIZE 6144
#define VOICE_SESSION_TASK_PRIORITY 6

/** @brief Sender wake cadence; bounds how long an audio frame can wait. */
#define VOICE_SESSION_SENDER_POLL_MS 10

/** @brief Bounded retries while the TLS handshake finishes and a frame is due. */
#define VOICE_SESSION_CONTROL_RETRY_ATTEMPTS 60

/** @brief Payload the sender uses to signal a shutdown. */
#define VOICE_SESSION_CONTROL_TYPE_NONE 0u
#define VOICE_SESSION_CONTROL_TYPE_TEXT 1u
#define VOICE_SESSION_CONTROL_TYPE_AUDIO 2u
#define VOICE_SESSION_CONTROL_TYPE_WAKE 3u

typedef struct {
    uint32_t type;
    size_t length;
    uint8_t data[VOICE_SESSION_CONTROL_FRAME_SIZE];
} voice_session_control_item_t;

typedef struct {
    uint32_t sequence;
    uint64_t captured_at_ms;
    size_t length;
    uint8_t data[AUDIO_CODEC_MAX_PACKET_BYTES];
} voice_session_audio_item_t;

typedef enum {
    VOICE_SESSION_PENDING_NONE = 0,
    VOICE_SESSION_PENDING_WAKE,
} voice_session_pending_t;

typedef struct {
    bool is_initialized;
    bool is_connected;
    bool has_active_session;
    bool capture_started;
    bool wake_suspended;
    bool is_speaking;
    voice_session_state_t state;

    char session_id[VOICE_SESSION_IDENTIFIER_SIZE];
    char stream_id[VOICE_SESSION_IDENTIFIER_SIZE];
    char device_id[VOICE_SESSION_IDENTIFIER_SIZE];
    char last_reason[VOICE_SESSION_REASON_SIZE];
    char wake_word[65];
    uint32_t wake_confidence_milli;
    voice_session_pending_t pending;

    uint32_t next_audio_sequence;
    uint32_t last_wake_detection_count;

#if VOICE_SESSION_HAS_PARENT_CONTROL_RUNTIME
    // Monotonic start of the active conversation, used to derive billed
    // conversation seconds without trusting wall-clock subtraction.
    int64_t conversation_started_ms;
    bool conversation_tracked;
#endif

    esp_websocket_client_handle_t client;
    SemaphoreHandle_t lifecycle_mutex;
    QueueHandle_t control_queue;
    QueueHandle_t audio_queue;
    TaskHandle_t sender_task;
    volatile bool sender_running;

    voice_session_snapshot_t counters;
} voice_session_runtime_t;

static voice_session_runtime_t voice_session_state;
static portMUX_TYPE voice_session_counter_lock = portMUX_INITIALIZER_UNLOCKED;

static void voice_session_increment(uint32_t *counter) {
    portENTER_CRITICAL(&voice_session_counter_lock);
    (*counter)++;
    portEXIT_CRITICAL(&voice_session_counter_lock);
}

static void voice_session_publish_state(voice_session_runtime_t *state) {
    portENTER_CRITICAL(&voice_session_counter_lock);
    voice_session_state.counters.state = state->state;
    voice_session_state.counters.is_connected = state->is_connected;
    voice_session_state.counters.has_active_session = state->has_active_session;
    portEXIT_CRITICAL(&voice_session_counter_lock);
}

// FNV-1a over the bounded session id. The playback queue item id is much
// shorter than a session id, so the id is folded into a small stable value
// instead of being copied and truncated.
static uint32_t voice_session_hash_string(const char *value) {
    uint32_t hash = 2166136261u;
    if (value == NULL) {
        return hash;
    }
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != '\0';
         ++cursor) {
        hash ^= (uint32_t)(*cursor);
        hash *= 16777619u;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Endpoint and authentication
// ---------------------------------------------------------------------------

static bool voice_session_endpoint_is_secure(const char *endpoint) {
    if (endpoint == NULL) {
        return false;
    }
    if (strncmp(endpoint, "wss://", 6) == 0) {
        return true;
    }
#if CONFIG_VOICE_SESSION_ALLOW_INSECURE_WS
    // Cleartext is only reachable from a debug build that is not a release
    // image. Production firmware never compiles this branch.
#if !CONFIG_COMPILER_OPTIMIZATION_LEVEL_RELEASE
    if (strncmp(endpoint, "ws://", 5) == 0) {
        return true;
    }
#endif
#endif
    return false;
}

static esp_err_t voice_session_copy_token(char *output, size_t output_size) {
#if CONFIG_FEATURE_CLOUD_AUTH
    // A fresh short-lived token is requested before every connection attempt
    // so an expired token never reaches the gateway.
    (void)cloud_auth_ensure_authenticated();
#endif
#if CONFIG_FEATURE_DEVICE_BINDING_CLIENT
    return device_binding_client_copy_session_token(output, output_size);
#else
    (void)output;
    (void)output_size;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

// ---------------------------------------------------------------------------
// Control framing
// ---------------------------------------------------------------------------

static void voice_session_format_timestamp(char *output, size_t output_size) {
    // RFC3339 with a fixed UTC form; the clock is synchronized before a
    // session starts, so the value is meaningful to the gateway.
    const time_t now = time(NULL);
    struct tm utc = {0};
    gmtime_r(&now, &utc);
    strftime(
        output,
        output_size,
        "%Y-%m-%dT%H:%M:%SZ",
        &utc
    );
    if (output[0] == '\0') {
        memcpy(output, "1970-01-01T00:00:00Z", 21);
    }
}

static cJSON *voice_session_control_base(const char *type) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    char timestamp[VOICE_SESSION_TIMESTAMP_SIZE] = {0};
    voice_session_format_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(
        root,
        "schema_version",
        VOICE_SESSION_PROTOCOL_SCHEMA_VERSION
    );
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(
        root,
        "session_id",
        voice_session_state.session_id
    );
    cJSON_AddStringToObject(
        root,
        "device_id",
        voice_session_state.device_id
    );
    cJSON_AddStringToObject(root, "sent_at", timestamp);
    return root;
}

static void voice_session_add_capabilities(cJSON *root) {
#if CONFIG_FEATURE_DEVICE_CAPABILITIES
    cJSON *capabilities = cJSON_AddArrayToObject(root, "capabilities");
    if (capabilities == NULL) {
        return;
    }
    const device_capability_bitmap_t bitmap = device_capabilities_get();
    for (device_capability_t capability = DEVICE_CAPABILITY_AUDIO_INPUT;
         capability < DEVICE_CAPABILITY_COUNT;
         ++capability) {
        if (device_capabilities_has(bitmap, capability)) {
            const char *name = device_capability_name(capability);
            if (name != NULL) {
                cJSON_AddItemToArray(
                    capabilities,
                    cJSON_CreateString(name)
                );
            }
        }
    }
#else
    (void)root;
#endif
}

static size_t voice_session_serialize(
    cJSON *root,
    uint8_t *output,
    size_t output_size
) {
    if (root == NULL || output == NULL || output_size == 0) {
        cJSON_Delete(root);
        return 0;
    }
    char *encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (encoded == NULL) {
        return 0;
    }
    const size_t length = strlen(encoded);
    if (length == 0 || length >= output_size) {
        cJSON_free(encoded);
        return 0;
    }
    memcpy(output, encoded, length);
    cJSON_free(encoded);
    return length;
}

static size_t voice_session_build_session_start(
    uint8_t *output,
    size_t output_size
) {
    cJSON *root = voice_session_control_base("session_start");
    if (root == NULL) {
        return 0;
    }
    cJSON_AddStringToObject(root, "stream_id", voice_session_state.stream_id);
    cJSON_AddNumberToObject(
        root,
        "sample_rate_hz",
        AUDIO_CODEC_SAMPLE_RATE_HZ
    );
    cJSON_AddNumberToObject(root, "channel_count", AUDIO_CODEC_CHANNEL_COUNT);
    cJSON_AddNumberToObject(
        root,
        "duration_ms",
        AUDIO_CODEC_FRAME_DURATION_MS
    );
    cJSON_AddStringToObject(root, "encoding", AUDIO_CODEC_ENCODING_NAME);

    const char *firmware_version = "0.0.0";
#if CONFIG_FEATURE_VERSION_INFO
    const version_info_t version = version_info_get();
    if (version.firmware_version != NULL &&
        version.firmware_version[0] != '\0') {
        firmware_version = version.firmware_version;
    }
#endif
    cJSON_AddStringToObject(root, "firmware_version", firmware_version);
    voice_session_add_capabilities(root);
    return voice_session_serialize(root, output, output_size);
}

static size_t voice_session_build_wake_detected(
    const char *wake_word,
    uint32_t confidence_milli,
    uint8_t *output,
    size_t output_size
) {
    cJSON *root = voice_session_control_base("wake_detected");
    if (root == NULL) {
        return 0;
    }
    cJSON_AddStringToObject(root, "stream_id", voice_session_state.stream_id);
    cJSON_AddStringToObject(root, "wake_word", wake_word);
    cJSON_AddNumberToObject(
        root,
        "wake_confidence_milli",
        (double)confidence_milli
    );
    return voice_session_serialize(root, output, output_size);
}

static size_t voice_session_build_reason_frame(
    const char *type,
    const char *reason,
    uint8_t *output,
    size_t output_size
) {
    cJSON *root = voice_session_control_base(type);
    if (root == NULL) {
        return 0;
    }
    cJSON_AddStringToObject(root, "reason", reason);
    return voice_session_serialize(root, output, output_size);
}

// ---------------------------------------------------------------------------
// Sender task and capture callback
// ---------------------------------------------------------------------------

static bool voice_session_send_text_payload(
    const uint8_t *payload,
    size_t length
) {
    if (voice_session_state.client == NULL || payload == NULL || length == 0) {
        return false;
    }
    if (!esp_websocket_client_is_connected(voice_session_state.client)) {
        return false;
    }
    const int sent = esp_websocket_client_send_text(
        voice_session_state.client,
        (const char *)payload,
        (int)length,
        pdMS_TO_TICKS(2000)
    );
    if (sent < 0 || (size_t)sent != length) {
        voice_session_increment(
            &voice_session_state.counters.control_frames_dropped
        );
        return false;
    }
    voice_session_increment(&voice_session_state.counters.control_frames_sent);
    return true;
}

static void voice_session_send_audio_payload(
    const voice_session_audio_item_t *item
) {
    if (item == NULL || voice_session_state.client == NULL ||
        !voice_session_state.has_active_session) {
        return;
    }
    if (!esp_websocket_client_is_connected(voice_session_state.client)) {
        return;
    }

    uint8_t envelope[AUDIO_CODEC_MAX_PACKET_BYTES +
                     VOICE_SESSION_ENVELOPE_HEADER_SIZE +
                     VOICE_SESSION_IDENTIFIER_SIZE] = {0};
    size_t envelope_length = 0;
    const voice_session_protocol_error_t result =
        voice_session_envelope_encode(
            VOICE_SESSION_ENVELOPE_MAGIC_DEVICE_AUDIO,
            voice_session_state.session_id,
            item->sequence,
            item->captured_at_ms,
            item->data,
            item->length,
            envelope,
            sizeof(envelope),
            &envelope_length
        );
    if (result != VOICE_SESSION_PROTOCOL_OK) {
        voice_session_increment(&voice_session_state.counters.audio_frames_dropped);
        return;
    }
    const int sent = esp_websocket_client_send_bin(
        voice_session_state.client,
        (const char *)envelope,
        (int)envelope_length,
        pdMS_TO_TICKS(2000)
    );
    if (sent < 0 || (size_t)sent != envelope_length) {
        voice_session_increment(&voice_session_state.counters.audio_frames_dropped);
        return;
    }
    voice_session_increment(&voice_session_state.counters.audio_frames_sent);
}

/**
 * @brief Detect a new wake event without replacing wake_feedback's handler.
 *
 * voice_wake exposes only one handler, and wake_feedback already owns it for
 * the cue, LED, and telemetry path. Overwriting it would silently disable
 * wake feedback, so the sender polls the detector counter and reacts to each
 * new detection. The poll runs on the sender task, so it never blocks audio.
 */
static void voice_session_poll_wake(void) {
#if CONFIG_FEATURE_VOICE_WAKE
    voice_wake_snapshot_t snapshot;
    if (voice_wake_get_snapshot(&snapshot) != ESP_OK) {
        return;
    }
    if (snapshot.detections == voice_session_state.last_wake_detection_count) {
        return;
    }
    voice_session_state.last_wake_detection_count = snapshot.detections;
    (void)voice_session_notify_wake_detected(
        "ni hao xiao zhi",
        snapshot.last_confidence_milli
    );
#endif
}

static void voice_session_sender_task(void *argument) {
    (void)argument;
    voice_session_control_item_t control;
    voice_session_audio_item_t audio;

    while (voice_session_state.sender_running) {
        voice_session_poll_wake();
#if VOICE_SESSION_HAS_PARENT_CONTROL_RUNTIME
        if (voice_session_state.has_active_session) {
            parent_control_decision_t decision = {};
            // Continuation check: the session already recorded its blocked
            // attempt at start, so this tick must not re-count it.
            if (parent_control_evaluate_active(
                    NULL,
                    (int64_t)time(NULL),
                    time_sync_get_timezone_offset_minutes(),
                    &decision
                ) == ESP_OK &&
                parent_control_decision_is_denied(&decision)) {
                (void)voice_session_end_session(decision.reason_code);
            } else {
                (void)usage_ledger_set_active(
                    true,
                    (int64_t)time(NULL),
                    time_sync_get_timezone_offset_minutes()
                );
            }
        } else {
            (void)usage_ledger_set_active(
                false,
                (int64_t)time(NULL),
                time_sync_get_timezone_offset_minutes()
            );
        }
#endif
        if (xQueueReceive(
                voice_session_state.control_queue,
                &control,
                pdMS_TO_TICKS(VOICE_SESSION_SENDER_POLL_MS)
            ) == pdTRUE) {
            if (!voice_session_state.sender_running) {
                break;
            }
            if (control.type == VOICE_SESSION_CONTROL_TYPE_TEXT ||
                control.type == VOICE_SESSION_CONTROL_TYPE_WAKE) {
                // The handshake may complete a moment after the frame is
                // queued, so retry a bounded number of times instead of
                // dropping the frame while the TLS session is still opening.
                bool sent = false;
                for (int attempt = 0;
                     attempt < VOICE_SESSION_CONTROL_RETRY_ATTEMPTS &&
                     voice_session_state.sender_running;
                     ++attempt) {
                    if (voice_session_send_text_payload(
                            control.data,
                            control.length
                        )) {
                        sent = true;
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                if (!sent) {
                    voice_session_increment(
                        &voice_session_state.counters.control_frames_dropped
                    );
                } else if (control.type == VOICE_SESSION_CONTROL_TYPE_WAKE) {
                    voice_session_increment(
                        &voice_session_state.counters.wake_events
                    );
                }
            }
        }
        // Drain audio only while a session exists and the socket is up. The
        // queue is bounded, so a stalled socket drops frames instead of
        // growing memory.
        while (xQueueReceive(voice_session_state.audio_queue, &audio, 0) ==
               pdTRUE) {
            if (voice_session_state.has_active_session) {
                voice_session_send_audio_payload(&audio);
            }
        }
        // Retry a deferred wake frame once the gateway acknowledged the
        // session, because the gateway rejects wake_detected without one.
        if (voice_session_state.pending == VOICE_SESSION_PENDING_WAKE &&
            voice_session_state.has_active_session) {
            uint8_t frame[VOICE_SESSION_CONTROL_FRAME_SIZE] = {0};
            const size_t length = voice_session_build_wake_detected(
                voice_session_state.wake_word,
                voice_session_state.wake_confidence_milli,
                frame,
                sizeof(frame)
            );
            if (length > 0) {
                voice_session_state.pending = VOICE_SESSION_PENDING_NONE;
                if (voice_session_send_text_payload(frame, length)) {
                    voice_session_increment(
                        &voice_session_state.counters.wake_events
                    );
                }
            }
        }
    }
    voice_session_state.sender_task = NULL;
    vTaskDelete(NULL);
}

static void voice_session_on_audio(
    const audio_codec_packet_t *packet,
    void *context
) {
    (void)context;
    if (packet == NULL) {
        return;
    }
    // Runs on the audio_input capture task: only bounded, non-blocking work.
    if (!voice_session_state.has_active_session) {
        return;
    }

    // Barge-in policy: capture stays on in every state so the child can
    // interrupt. While the gateway is speaking, a frame only reaches it once
    // the local VAD has marked speech; that first speech frame flushes local
    // conversation playback without dropping safety frames and is forwarded so
    // the gateway can stop its own synthesis. While listening or thinking the
    // VAD-positive frame is forwarded directly, because audio_input already
    // suppresses non-speech.
    if (voice_session_state.state == VOICE_SESSION_STATE_SPEAKING) {
        voice_session_state.is_speaking = false;
        (void)playback_queue_clear(false);
        voice_session_increment(&voice_session_state.counters.barge_in_events);
    }

    voice_session_audio_item_t item;
    memset(&item, 0, sizeof(item));
    item.sequence = voice_session_state.next_audio_sequence++;
    item.captured_at_ms = packet->captured_at_ms;
    item.length = packet->packet_size;
    if (item.length > sizeof(item.data)) {
        voice_session_increment(&voice_session_state.counters.audio_frames_dropped);
        return;
    }
    memcpy(item.data, packet->packet, item.length);
    if (xQueueSend(voice_session_state.audio_queue, &item, 0) != pdTRUE) {
        voice_session_increment(&voice_session_state.counters.audio_frames_dropped);
    }
}

// ---------------------------------------------------------------------------
// Inbound control handling
// ---------------------------------------------------------------------------

static void voice_session_set_state(voice_session_state_t state) {
    voice_session_state.state = state;
    voice_session_state.is_speaking =
        state == VOICE_SESSION_STATE_SPEAKING;
    voice_session_publish_state(&voice_session_state);
}

#if VOICE_SESSION_HAS_PARENT_CONTROL_RUNTIME
/**
 * @brief Record the finished conversation against the local usage day.
 *
 * The elapsed seconds come from the monotonic timer captured at session start,
 * so a wall-clock correction cannot create negative or inflated conversation
 * totals. The pending flag is cleared so the terminal paths that race on a
 * dropped socket cannot double count one conversation.
 */
static void voice_session_finalize_conversation(void) {
    if (!voice_session_state.conversation_tracked) {
        return;
    }
    voice_session_state.conversation_tracked = false;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    uint32_t elapsed_seconds = 0;
    if (now_ms > voice_session_state.conversation_started_ms) {
        elapsed_seconds = (uint32_t)(
            (now_ms - voice_session_state.conversation_started_ms) / 1000
        );
    }
    voice_session_state.conversation_started_ms = 0;
    (void)parent_control_note_conversation_finished(
        (int64_t)time(NULL),
        time_sync_get_timezone_offset_minutes(),
        elapsed_seconds
    );
}
#else
static void voice_session_finalize_conversation(void) {
}
#endif

static void voice_session_handle_session_started(void) {
    voice_session_state.has_active_session = true;
#if VOICE_SESSION_HAS_PARENT_CONTROL_RUNTIME
    voice_session_state.conversation_started_ms = esp_timer_get_time() / 1000;
    voice_session_state.conversation_tracked = true;
#endif
    voice_session_set_state(VOICE_SESSION_STATE_LISTENING);
#if CONFIG_FEATURE_CONVERSATION_CONTEXT
    // Label the session with the local content level so the device can show
    // and gate the recent turns. The gateway owns the authoritative history,
    // so this is a bounded local marker rather than a second source of truth.
    char tier_text[16] = "unknown";
#if CONFIG_FEATURE_CHILD_PROMPT_PROFILE
    (void)child_prompt_profile_copy_tier_text(
        tier_text,
        sizeof(tier_text)
    );
#endif
    char summary[CONVERSATION_CONTEXT_SUMMARY_SIZE + 1] = {0};
    snprintf(
        summary,
        sizeof(summary),
        "session_started tier=%s",
        tier_text
    );
    (void)conversation_context_append("system", summary);
#endif
}

static void voice_session_handle_session_state(const cJSON *root) {
    const cJSON *state_item = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (!cJSON_IsString(state_item) || state_item->valuestring == NULL) {
        return;
    }
    voice_session_protocol_state_t protocol_state =
        VOICE_SESSION_PROTOCOL_STATE_IDLE;
    if (!voice_session_state_from_name(
            state_item->valuestring,
            &protocol_state
        )) {
        return;
    }
    switch (protocol_state) {
        case VOICE_SESSION_PROTOCOL_STATE_IDLE:
            voice_session_set_state(VOICE_SESSION_STATE_IDLE);
            break;
        case VOICE_SESSION_PROTOCOL_STATE_LISTENING:
            voice_session_set_state(VOICE_SESSION_STATE_LISTENING);
            break;
        case VOICE_SESSION_PROTOCOL_STATE_THINKING:
            voice_session_set_state(VOICE_SESSION_STATE_THINKING);
            break;
        case VOICE_SESSION_PROTOCOL_STATE_SPEAKING:
            voice_session_set_state(VOICE_SESSION_STATE_SPEAKING);
            break;
        default:
            break;
    }
}

static void voice_session_handle_session_closed(const cJSON *root) {
    const cJSON *reason_item = cJSON_GetObjectItemCaseSensitive(root, "reason");
    if (cJSON_IsString(reason_item) && reason_item->valuestring != NULL) {
        strncpy(
            voice_session_state.last_reason,
            reason_item->valuestring,
            sizeof(voice_session_state.last_reason) - 1
        );
    }
    voice_session_finalize_conversation();
    voice_session_state.has_active_session = false;
    voice_session_set_state(VOICE_SESSION_STATE_CLOSED);
}

static void voice_session_handle_error(const cJSON *root) {
    const cJSON *code_item = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (cJSON_IsString(code_item) && code_item->valuestring != NULL) {
        strncpy(
            voice_session_state.last_reason,
            code_item->valuestring,
            sizeof(voice_session_state.last_reason) - 1
        );
    }
    ESP_LOGW(TAG, "gateway error: %s", voice_session_state.last_reason);
    voice_session_set_state(VOICE_SESSION_STATE_FAILED);
}

static void voice_session_handle_control_text(
    const uint8_t *data,
    size_t length
) {
    if (data == NULL || length == 0 ||
        length >= VOICE_SESSION_CONTROL_FRAME_SIZE) {
        return;
    }
    // Copy into a bounded NUL-terminated buffer; the event payload is not
    // guaranteed to be terminated.
    char buffer[VOICE_SESSION_CONTROL_FRAME_SIZE] = {0};
    memcpy(buffer, data, length);
    buffer[length] = '\0';

    cJSON *root = cJSON_ParseWithLength(buffer, length);
    if (root == NULL) {
        voice_session_increment(&voice_session_state.counters.decode_failures);
        return;
    }
    voice_session_increment(&voice_session_state.counters.control_frames_received);

    const cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type_item) && type_item->valuestring != NULL) {
        const char *type = type_item->valuestring;
        if (strcmp(type, "session_started") == 0) {
            voice_session_handle_session_started();
        } else if (strcmp(type, "session_state") == 0) {
            voice_session_handle_session_state(root);
        } else if (strcmp(type, "session_closed") == 0) {
            voice_session_handle_session_closed(root);
        } else if (strcmp(type, "error") == 0) {
            voice_session_handle_error(root);
        }
        // ping and pong control frames are answered by the WebSocket stack, so
        // the module only mirrors session-affecting frames here.
    }
    cJSON_Delete(root);
}

static void voice_session_handle_binary(
    const uint8_t *data,
    size_t length
) {
    if (data == NULL || length == 0 ||
        length < VOICE_SESSION_ENVELOPE_HEADER_SIZE) {
        return;
    }
    voice_session_envelope_t envelope;
    const voice_session_protocol_error_t result =
        voice_session_envelope_decode(
            data,
            length,
            VOICE_SESSION_ENVELOPE_MAGIC_SERVER_AUDIO,
            &envelope
        );
    if (result != VOICE_SESSION_PROTOCOL_OK) {
        voice_session_increment(&voice_session_state.counters.decode_failures);
        return;
    }
    if (!voice_session_state.has_active_session ||
        strcmp(envelope.session_id, voice_session_state.session_id) != 0) {
        return;
    }

    audio_codec_pcm_frame_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    if (audio_codec_decode_packet(
            envelope.payload,
            envelope.payload_length,
            &decoded
        ) != AUDIO_CODEC_OK) {
        voice_session_increment(&voice_session_state.counters.decode_failures);
        return;
    }
    voice_session_increment(&voice_session_state.counters.audio_frames_received);

    playback_queue_item_t item;
    memset(&item, 0, sizeof(item));
    // item_id is bounded to PLAYBACK_QUEUE_ITEM_ID_SIZE, while the session id
    // may be up to VOICE_SESSION_IDENTIFIER_SIZE. Fold the session id into a
    // short stable hash so the id always fits and stays unique per frame.
    snprintf(
        item.item_id,
        sizeof(item.item_id),
        "vs_%08lx_%08lx",
        (unsigned long)voice_session_hash_string(
            voice_session_state.session_id
        ),
        (unsigned long)envelope.sequence
    );
    item.priority = PLAYBACK_PRIORITY_CONVERSATION;
    item.is_interruptible = true;
    item.frame_count = 1;
    item.frames[0] = decoded;
    if (playback_queue_enqueue(&item) != PLAYBACK_QUEUE_OK) {
        voice_session_increment(&voice_session_state.counters.audio_frames_dropped);
    }
}

// ---------------------------------------------------------------------------
// WebSocket events
// ---------------------------------------------------------------------------

static void voice_session_websocket_event(
    void *handler_args,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
) {
    (void)handler_args;
    (void)event_base;
    esp_websocket_event_data_t *event =
        (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            voice_session_state.is_connected = true;
            voice_session_publish_state(&voice_session_state);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
        case WEBSOCKET_EVENT_CLOSED:
            voice_session_state.is_connected = false;
            if (voice_session_state.has_active_session) {
                voice_session_finalize_conversation();
                voice_session_set_state(VOICE_SESSION_STATE_CLOSED);
                voice_session_state.has_active_session = false;
                voice_session_increment(
                    &voice_session_state.counters.reconnect_attempts
                );
            }
            voice_session_publish_state(&voice_session_state);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (event == NULL || event->data_ptr == NULL ||
                event->data_len <= 0) {
                break;
            }
            // Only the first fragment of a message carries the opcode and the
            // full payload length; the remainder is dropped as bounded data.
            if (event->payload_offset != 0) {
                break;
            }
            if (event->op_code == 0x01) {
                voice_session_handle_control_text(
                    (const uint8_t *)event->data_ptr,
                    (size_t)event->data_len
                );
            } else if (event->op_code == 0x02) {
                voice_session_handle_binary(
                    (const uint8_t *)event->data_ptr,
                    (size_t)event->data_len
                );
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            voice_session_increment(&voice_session_state.counters.reconnect_attempts);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

static void voice_session_release_capture(void) {
    if (voice_session_state.capture_started) {
        (void)audio_input_stop();
        voice_session_state.capture_started = false;
    }
#if CONFIG_FEATURE_VOICE_WAKE
    if (voice_session_state.wake_suspended) {
        (void)voice_wake_resume();
        voice_session_state.wake_suspended = false;
    }
#endif
}

static void voice_session_stop_transport_locked(void) {
    if (voice_session_state.client != NULL) {
        (void)esp_websocket_client_close(
            voice_session_state.client,
            pdMS_TO_TICKS(1000)
        );
        (void)esp_websocket_client_stop(voice_session_state.client);
        (void)esp_websocket_client_destroy(voice_session_state.client);
        voice_session_state.client = NULL;
    }
    voice_session_state.is_connected = false;
}

static esp_err_t voice_session_prepare_context_locked(
    const char *stream_id,
    const char *session_id,
    voice_session_control_item_t *start_frame
) {
    const uint32_t random_value = esp_random();
    if (session_id != NULL) {
        if (!voice_session_identifier_is_valid(session_id)) {
            return ESP_ERR_INVALID_ARG;
        }
        strncpy(
            voice_session_state.session_id,
            session_id,
            sizeof(voice_session_state.session_id) - 1
        );
    } else if (!voice_session_identifier_generate(
                   "session",
                   random_value,
                   voice_session_state.session_id,
                   sizeof(voice_session_state.session_id)
               )) {
        return ESP_ERR_INVALID_ARG;
    }

    if (stream_id != NULL) {
        if (!voice_session_identifier_is_valid(stream_id)) {
            return ESP_ERR_INVALID_ARG;
        }
        strncpy(
            voice_session_state.stream_id,
            stream_id,
            sizeof(voice_session_state.stream_id) - 1
        );
    } else if (!voice_session_identifier_generate(
                   "stream",
                   random_value ^ 0x5A5A5A5Au,
                   voice_session_state.stream_id,
                   sizeof(voice_session_state.stream_id)
               )) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_FEATURE_DEVICE_IDENTITY
    if (device_identity_copy(
            voice_session_state.device_id,
            sizeof(voice_session_state.device_id)
        ) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif

    const size_t length = voice_session_build_session_start(
        start_frame->data,
        sizeof(start_frame->data)
    );
    if (length == 0) {
        return ESP_ERR_NO_MEM;
    }
    start_frame->type = VOICE_SESSION_CONTROL_TYPE_TEXT;
    start_frame->length = length;
    return ESP_OK;
}

static esp_err_t voice_session_pause_wake_locked(void) {
#if CONFIG_FEATURE_VOICE_WAKE
    if (voice_wake_suspend() == ESP_OK) {
        voice_session_state.wake_suspended = true;
    }
#endif
    return ESP_OK;
}

static esp_err_t voice_session_open_transport_locked(void) {
    char token[DEVICE_BINDING_SESSION_TOKEN_SIZE] = {0};
    esp_err_t token_result = voice_session_copy_token(token, sizeof(token));
    if (token_result != ESP_OK) {
        memset(token, 0, sizeof(token));
        return ESP_ERR_INVALID_STATE;
    }

    char headers[DEVICE_BINDING_SESSION_TOKEN_SIZE + 32] = {0};
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);
    memset(token, 0, sizeof(token));

    const esp_websocket_client_config_t config = {
        .uri = CONFIG_VOICE_SESSION_ENDPOINT,
        .headers = headers,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .task_prio = VOICE_SESSION_TASK_PRIORITY,
        .task_stack = VOICE_SESSION_TASK_STACK_SIZE,
        .buffer_size = VOICE_SESSION_WS_BUFFER_SIZE,
        .reconnect_timeout_ms = 2000,
        .network_timeout_ms = 10000,
    };
    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    memset(headers, 0, sizeof(headers));
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    voice_session_state.client = client;

    const esp_err_t register_result = esp_websocket_register_events(
        client,
        WEBSOCKET_EVENT_ANY,
        voice_session_websocket_event,
        NULL
    );
    if (register_result != ESP_OK) {
        voice_session_stop_transport_locked();
        return register_result;
    }
    const esp_err_t start_result = esp_websocket_client_start(client);
    if (start_result != ESP_OK) {
        voice_session_stop_transport_locked();
        return start_result;
    }
    return ESP_OK;
}

static esp_err_t voice_session_start_capture_locked(void) {
    const esp_err_t start_result = audio_input_start(
        0,
        voice_session_on_audio,
        NULL
    );
    if (start_result != ESP_OK) {
        return start_result;
    }
    voice_session_state.capture_started = true;
    return ESP_OK;
}

esp_err_t voice_session_start_session(
    const char *stream_id,
    const char *session_id
) {
#if VOICE_SESSION_HAS_PARENT_CONTROL_RUNTIME
    parent_control_decision_t decision = {};
    const parent_control_request_t policy_request = {
        .category = NULL,
        .safety_exempt = false,
        .utc_epoch_seconds = (int64_t)time(NULL),
        .timezone_offset_minutes = time_sync_get_timezone_offset_minutes(),
    };
    if (parent_control_evaluate(&policy_request, &decision) != ESP_OK ||
        parent_control_decision_is_denied(&decision)) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    if (!voice_session_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (voice_session_state.lifecycle_mutex == NULL ||
        xSemaphoreTake(
            voice_session_state.lifecycle_mutex,
            portMAX_DELAY
        ) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (voice_session_state.has_active_session ||
        voice_session_state.client != NULL) {
        result = ESP_ERR_INVALID_STATE;
    } else if (network_manager_get_state() !=
               NETWORK_MANAGER_STATE_CONNECTED) {
        result = ESP_ERR_INVALID_STATE;
    } else if (!voice_session_endpoint_is_secure(
                   CONFIG_VOICE_SESSION_ENDPOINT
               )) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        voice_session_control_item_t start_frame;
        memset(&start_frame, 0, sizeof(start_frame));
        result = voice_session_prepare_context_locked(
            stream_id,
            session_id,
            &start_frame
        );
        if (result == ESP_OK) {
            (void)voice_session_pause_wake_locked();
            result = voice_session_open_transport_locked();
        }
        if (result == ESP_OK) {
            result = voice_session_start_capture_locked();
        }
        if (result == ESP_OK) {
            voice_session_state.state = VOICE_SESSION_STATE_CONNECTING;
            voice_session_publish_state(&voice_session_state);
            if (xQueueSend(
                    voice_session_state.control_queue,
                    &start_frame,
                    pdMS_TO_TICKS(100)
                ) != pdTRUE) {
                result = ESP_ERR_NO_MEM;
            }
        }
        if (result != ESP_OK) {
            voice_session_release_capture();
            voice_session_stop_transport_locked();
        }
    }

    xSemaphoreGive(voice_session_state.lifecycle_mutex);
    return result;
}

esp_err_t voice_session_send_wake_detected(
    const char *wake_word,
    uint32_t confidence_milli
) {
    if (wake_word == NULL || wake_word[0] == '\0' ||
        strlen(wake_word) > 64 || confidence_milli > 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!voice_session_state.has_active_session) {
        return ESP_ERR_INVALID_STATE;
    }
    voice_session_control_item_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = VOICE_SESSION_CONTROL_TYPE_WAKE;
    frame.length = voice_session_build_wake_detected(
        wake_word,
        confidence_milli,
        frame.data,
        sizeof(frame.data)
    );
    if (frame.length == 0) {
        return ESP_ERR_NO_MEM;
    }
    if (xQueueSend(
            voice_session_state.control_queue,
            &frame,
            pdMS_TO_TICKS(100)
        ) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t voice_session_notify_wake_detected(
    const char *wake_word,
    uint32_t confidence_milli
) {
    if (wake_word == NULL || wake_word[0] == '\0' ||
        strlen(wake_word) > 64 || confidence_milli > 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    if (voice_session_state.has_active_session) {
        return voice_session_send_wake_detected(wake_word, confidence_milli);
    }

    // No active session: remember the wake so the sender can deliver it after
    // session_started. This matches the gateway, which rejects wake_detected
    // until a session exists.
    strncpy(
        voice_session_state.wake_word,
        wake_word,
        sizeof(voice_session_state.wake_word) - 1
    );
    voice_session_state.wake_confidence_milli = confidence_milli;
    const esp_err_t start_result = voice_session_start_session(NULL, NULL);
    if (start_result != ESP_OK) {
        return start_result;
    }
    voice_session_state.pending = VOICE_SESSION_PENDING_WAKE;
    return ESP_OK;
}

static esp_err_t voice_session_finish_session(
    const char *type,
    const char *reason,
    bool clear_safety
) {
    if (reason == NULL || reason[0] == '\0' || strlen(reason) > 128) {
        return ESP_ERR_INVALID_ARG;
    }
    if (voice_session_state.lifecycle_mutex == NULL ||
        xSemaphoreTake(
            voice_session_state.lifecycle_mutex,
            portMAX_DELAY
        ) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (voice_session_state.client == NULL) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        voice_session_control_item_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.type = VOICE_SESSION_CONTROL_TYPE_TEXT;
        frame.length = voice_session_build_reason_frame(
            type,
            reason,
            frame.data,
            sizeof(frame.data)
        );
        if (frame.length == 0) {
            result = ESP_ERR_NO_MEM;
        } else if (xQueueSend(
                       voice_session_state.control_queue,
                       &frame,
                       pdMS_TO_TICKS(100)
                   ) != pdTRUE) {
            result = ESP_ERR_NO_MEM;
        }
        // Give the sender a bounded window to flush the reason frame before
        // the socket is closed.
        vTaskDelay(pdMS_TO_TICKS(50));

        (void)playback_queue_clear(clear_safety);
        voice_session_release_capture();
        voice_session_stop_transport_locked();
        voice_session_finalize_conversation();
        voice_session_state.has_active_session = false;
        voice_session_set_state(VOICE_SESSION_STATE_CLOSED);
        strncpy(
            voice_session_state.last_reason,
            reason,
            sizeof(voice_session_state.last_reason) - 1
        );
    }

    xSemaphoreGive(voice_session_state.lifecycle_mutex);
    return result;
}

esp_err_t voice_session_end_session(const char *reason) {
    return voice_session_finish_session("session_end", reason, true);
}

esp_err_t voice_session_cancel(const char *reason) {
    return voice_session_finish_session("cancel", reason, false);
}

esp_err_t voice_session_get_snapshot(voice_session_snapshot_t *snapshot_out) {
    if (snapshot_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&voice_session_counter_lock);
    *snapshot_out = voice_session_state.counters;
    portEXIT_CRITICAL(&voice_session_counter_lock);
    return ESP_OK;
}

const char *voice_session_error_name(voice_session_error_t error) {
    switch (error) {
        case VOICE_SESSION_ERROR_NONE:
            return "none";
        case VOICE_SESSION_ERROR_NOT_INITIALIZED:
            return "not_initialized";
        case VOICE_SESSION_ERROR_NOT_READY:
            return "not_ready";
        case VOICE_SESSION_ERROR_INVALID_ARGUMENT:
            return "invalid_argument";
        case VOICE_SESSION_ERROR_INVALID_STATE:
            return "invalid_state";
        case VOICE_SESSION_ERROR_NO_MEMORY:
            return "no_memory";
        case VOICE_SESSION_ERROR_NOT_AUTHENTICATED:
            return "not_authenticated";
        case VOICE_SESSION_ERROR_ENDPOINT_INSECURE:
            return "endpoint_insecure";
        case VOICE_SESSION_ERROR_TRANSPORT:
            return "transport";
        case VOICE_SESSION_ERROR_PROTOCOL:
            return "protocol";
        default:
            return "unknown";
    }
}

const char *voice_session_state_name(voice_session_state_t state) {
    switch (state) {
        case VOICE_SESSION_STATE_IDLE:
            return "idle";
        case VOICE_SESSION_STATE_CONNECTING:
            return "connecting";
        case VOICE_SESSION_STATE_LISTENING:
            return "listening";
        case VOICE_SESSION_STATE_THINKING:
            return "thinking";
        case VOICE_SESSION_STATE_SPEAKING:
            return "speaking";
        case VOICE_SESSION_STATE_CLOSED:
            return "closed";
        case VOICE_SESSION_STATE_FAILED:
            return "failed";
        default:
            return "unknown";
    }
}

esp_err_t voice_session_init(void) {
    if (voice_session_state.is_initialized) {
        return ESP_OK;
    }
    if (!audio_codec_is_ready() || !playback_queue_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!voice_session_endpoint_is_secure(CONFIG_VOICE_SESSION_ENDPOINT)) {
        return ESP_ERR_INVALID_ARG;
    }

    voice_session_state.lifecycle_mutex = xSemaphoreCreateMutex();
    voice_session_state.control_queue = xQueueCreate(
        VOICE_SESSION_CONTROL_QUEUE_DEPTH,
        sizeof(voice_session_control_item_t)
    );
    voice_session_state.audio_queue = xQueueCreate(
        VOICE_SESSION_AUDIO_QUEUE_DEPTH,
        sizeof(voice_session_audio_item_t)
    );
    if (voice_session_state.lifecycle_mutex == NULL ||
        voice_session_state.control_queue == NULL ||
        voice_session_state.audio_queue == NULL) {
        voice_session_shutdown();
        return ESP_ERR_NO_MEM;
    }

    voice_session_state.sender_running = true;
    if (xTaskCreate(
            voice_session_sender_task,
            "voice_session",
            VOICE_SESSION_TASK_STACK_SIZE,
            NULL,
            VOICE_SESSION_TASK_PRIORITY,
            &voice_session_state.sender_task
        ) != pdPASS) {
        voice_session_state.sender_running = false;
        voice_session_shutdown();
        return ESP_ERR_NO_MEM;
    }

    voice_session_state.state = VOICE_SESSION_STATE_IDLE;
    voice_session_state.is_initialized = true;
    portENTER_CRITICAL(&voice_session_counter_lock);
    memset(
        &voice_session_state.counters,
        0,
        sizeof(voice_session_state.counters)
    );
    voice_session_state.counters.is_initialized = true;
    portEXIT_CRITICAL(&voice_session_counter_lock);
    voice_session_publish_state(&voice_session_state);
    return ESP_OK;
}

bool voice_session_is_ready(void) {
    return voice_session_state.is_initialized;
}

void voice_session_shutdown(void) {
    // Stop microphone capture first so no producer can touch the audio queue
    // after it is released.
    voice_session_release_capture();

    // Then stop the sender. It owns the socket, so it must be quiesced before
    // the transport and queues are destroyed.
    voice_session_state.sender_running = false;
    if (voice_session_state.control_queue != NULL) {
        voice_session_control_item_t wake;
        memset(&wake, 0, sizeof(wake));
        wake.type = VOICE_SESSION_CONTROL_TYPE_NONE;
        (void)xQueueSend(voice_session_state.control_queue, &wake, 0);
    }
    // The sender can be inside a bounded socket send, so wait long enough for
    // the worst case before deciding the resources are safe to release.
    for (int attempt = 0;
         attempt < 300 && voice_session_state.sender_task != NULL;
         ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (voice_session_state.lifecycle_mutex != NULL &&
        xSemaphoreTake(
            voice_session_state.lifecycle_mutex,
            portMAX_DELAY
        ) == pdTRUE) {
        voice_session_stop_transport_locked();
        voice_session_finalize_conversation();
        voice_session_state.has_active_session = false;
        xSemaphoreGive(voice_session_state.lifecycle_mutex);
    }

    // If the sender did not exit, deleting the queues or the socket handle
    // would be a use-after-free. Leave the bounded resources in place and
    // report the incomplete teardown instead.
    if (voice_session_state.sender_task != NULL) {
        ESP_LOGE(TAG, "sender task did not stop; resources retained");
    } else {
        if (voice_session_state.control_queue != NULL) {
            vQueueDelete(voice_session_state.control_queue);
            voice_session_state.control_queue = NULL;
        }
        if (voice_session_state.audio_queue != NULL) {
            vQueueDelete(voice_session_state.audio_queue);
            voice_session_state.audio_queue = NULL;
        }
        if (voice_session_state.lifecycle_mutex != NULL) {
            vSemaphoreDelete(voice_session_state.lifecycle_mutex);
            voice_session_state.lifecycle_mutex = NULL;
        }
    }
    voice_session_state.is_initialized = false;
    voice_session_state.is_connected = false;
    voice_session_state.capture_started = false;
    voice_session_state.wake_suspended = false;
    voice_session_state.client = NULL;

    portENTER_CRITICAL(&voice_session_counter_lock);
    voice_session_state.counters.is_initialized = false;
    voice_session_state.counters.is_connected = false;
    voice_session_state.counters.has_active_session = false;
    portEXIT_CRITICAL(&voice_session_counter_lock);
}

const module_descriptor_t *voice_session_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "voice_session",
        .version = "1.0.0",
        .initialize = voice_session_init,
        .shutdown = voice_session_shutdown,
    };
    return &descriptor;
}
