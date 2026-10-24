#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_codec.h"
#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Longest session or stream identifier including the terminator. */
#define VOICE_SESSION_IDENTIFIER_SIZE 128

/** @brief Longest stable failure reason copied into the snapshot. */
#define VOICE_SESSION_REASON_SIZE 64

/** @brief Number of quality counters retained for the latest conversation turn. */
#define VOICE_SESSION_QUALITY_FRAME_COUNT 4

/**
 * @brief Transport and orchestration errors returned by voice_session.
 *
 * Values are stable so diagnostics and the parent app can map them without
 * relying on log text.
 */
typedef enum {
    VOICE_SESSION_ERROR_NONE = 0,
    VOICE_SESSION_ERROR_NOT_INITIALIZED,
    VOICE_SESSION_ERROR_NOT_READY,
    VOICE_SESSION_ERROR_INVALID_ARGUMENT,
    VOICE_SESSION_ERROR_INVALID_STATE,
    VOICE_SESSION_ERROR_NO_MEMORY,
    VOICE_SESSION_ERROR_NOT_AUTHENTICATED,
    VOICE_SESSION_ERROR_ENDPOINT_INSECURE,
    VOICE_SESSION_ERROR_TRANSPORT,
    VOICE_SESSION_ERROR_PROTOCOL,
} voice_session_error_t;

/** @brief Gateway session state mirrored from session_state control frames. */
typedef enum {
    VOICE_SESSION_STATE_IDLE = 0,
    VOICE_SESSION_STATE_CONNECTING,
    VOICE_SESSION_STATE_LISTENING,
    VOICE_SESSION_STATE_THINKING,
    VOICE_SESSION_STATE_SPEAKING,
    VOICE_SESSION_STATE_CLOSED,
    VOICE_SESSION_STATE_FAILED,
} voice_session_state_t;

/** @brief Bounded counters and state for diagnostics and the device UI. */
typedef struct {
    bool is_initialized;
    bool is_connected;
    bool has_active_session;
    voice_session_state_t state;
    char session_id[VOICE_SESSION_IDENTIFIER_SIZE];
    char stream_id[VOICE_SESSION_IDENTIFIER_SIZE];
    char last_reason[VOICE_SESSION_REASON_SIZE];
    uint32_t control_frames_sent;
    uint32_t control_frames_received;
    uint32_t control_frames_dropped;
    uint32_t audio_frames_sent;
    uint32_t audio_frames_received;
    uint32_t audio_frames_dropped;
    uint32_t video_frames_sent;
    uint32_t video_frames_dropped;
    uint32_t decode_failures;
    uint32_t barge_in_events;
    uint32_t reconnect_attempts;
    uint32_t wake_events;
    uint32_t last_error;
} voice_session_snapshot_t;

/** @brief Non-audio quality metrics for one conversation turn. */
typedef struct {
    bool is_available;
    uint32_t captured_frames;
    uint32_t speech_frames;
    uint32_t suppressed_frames;
    uint32_t double_talk_frames;
    uint32_t aec_convergence_q10;
    uint32_t agc_gain_q8;
    uint32_t barge_in_events;
    uint32_t self_echo_rejections;
    uint16_t last_microphone_level_q15;
    uint16_t last_reference_level_q15;
    uint32_t sample_count;
    uint32_t samples[VOICE_SESSION_QUALITY_FRAME_COUNT];
    uint32_t baseline_captured_frames;
    uint32_t baseline_speech_frames;
    uint32_t baseline_suppressed_frames;
    uint32_t baseline_double_talk_frames;
} voice_session_quality_snapshot_t;

/**
 * @brief Initialize the WSS transport and orchestration resources.
 *
 * Requires network, TLS, the codec, playback, and device identity modules.
 * No socket is opened until a session starts, so an idle device holds no
 * connection and never captures audio.
 */
esp_err_t voice_session_init(void);

/** @brief Return true when the module is ready to start a session. */
bool voice_session_is_ready(void);

/**
 * @brief Start a conversation session.
 *
 * Acquires the device session token, connects over WSS, sends session_start,
 * and starts microphone capture. Passing NULL for stream_id lets the module
 * generate one. The session identifier is generated when session_id is NULL.
 * Returns ESP_ERR_INVALID_STATE when a session is already active.
 */
esp_err_t voice_session_start_session(
    const char *stream_id,
    const char *session_id
);

/**
 * @brief Report a locally detected wake word.
 *
 * When no session is active the module starts a session and then sends
 * wake_detected once the gateway acknowledges session_started, matching the
 * gateway state machine. When a session is active the frame is sent directly.
 */
esp_err_t voice_session_notify_wake_detected(
    const char *wake_word,
    uint32_t confidence_milli
);

/** @brief Send wake_detected for the active session. */
esp_err_t voice_session_send_wake_detected(
    const char *wake_word,
    uint32_t confidence_milli
);

/**
 * @brief Send one encoded JPEG still over the active session transport.
 *
 * Blocks while the frame is copied and written to the socket, so it must be
 * called from a task that may wait, never from an audio or camera ISR. The
 * frame is gated by IMAGE-upload consent, not audio consent, and is dropped
 * and counted when consent is missing. The envelope is heap-allocated because
 * a still is far too large for a task stack, and is released on every path.
 *
 * @param jpeg Encoded JPEG bytes; copied before the call returns.
 * @param length Byte count, at most VOICE_SESSION_MAX_VIDEO_PAYLOAD_BYTES.
 * @param captured_at_ms Capture time in milliseconds for the envelope.
 * @return ESP_OK when the frame was sent;
 *         ESP_ERR_INVALID_ARG for a NULL pointer or zero length;
 *         ESP_ERR_INVALID_SIZE when length exceeds the video payload cap;
 *         ESP_ERR_INVALID_STATE when no session is active, the socket is not
 *         connected, or the socket write failed;
 *         ESP_ERR_NO_MEM when the envelope could not be allocated.
 *         A missing-consent, allocation, or write failure drops the frame and
 *         counts it in video_frames_dropped.
 */
esp_err_t voice_session_send_video_frame(
    const uint8_t *jpeg,
    size_t length,
    uint32_t captured_at_ms
);

/** @brief End the active session with a stable reason. */
esp_err_t voice_session_end_session(const char *reason);

/**
 * @brief Cancel the active session and tear down the transport.
 *
 * Sends cancel, clears unsuppressible conversation playback while preserving
 * safety frames, and returns the module to the idle state.
 */
esp_err_t voice_session_cancel(const char *reason);

/** @brief Copy the bounded session snapshot. */
esp_err_t voice_session_get_snapshot(voice_session_snapshot_t *snapshot_out);

/** @brief Copy the latest bounded far-field quality metrics. */
esp_err_t voice_session_get_quality_snapshot(
    voice_session_quality_snapshot_t *snapshot_out
);

/** @brief Return the stable string for one voice session error. */
const char *voice_session_error_name(voice_session_error_t error);

/** @brief Return the stable string for one mirrored session state. */
const char *voice_session_state_name(voice_session_state_t state);

/**
 * @brief Tear down the session, close the socket, and release resources.
 *
 * Intended for module_registry shutdown and test teardown. The module returns
 * to the uninitialized state and can be reinitialized only after every owned
 * task and transport has actually exited. If capture or the sender misses its
 * stop deadline, resources are retained and initialization is refused.
 */
void voice_session_shutdown(void);

/** @brief Return the removable-module descriptor for voice_session. */
const module_descriptor_t *voice_session_module_descriptor(void);

#ifdef __cplusplus
}
#endif
