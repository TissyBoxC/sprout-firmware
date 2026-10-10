#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable reasons the duplex state machine emits an event. */
typedef enum {
    VOICE_DUPLEX_EVENT_NONE = 0,
    VOICE_DUPLEX_EVENT_BARGE_IN,
    VOICE_DUPLEX_EVENT_IDLE_TIMEOUT,
} voice_duplex_event_t;

/** @brief Pure configuration for barge-in and continuous-session timing. */
typedef struct {
    uint16_t activity_threshold_q15;
    uint16_t echo_guard_threshold_q15;
    uint16_t barge_in_frames;
    uint32_t idle_timeout_ms;
    uint32_t playback_activity_hold_ms;
} voice_duplex_config_t;

/** @brief Bounded state visible to diagnostics and tests. */
typedef struct {
    voice_duplex_config_t config;
    bool active;
    bool playback_active;
    bool barge_in_armed;
    uint32_t consecutive_activity_frames;
    uint32_t last_activity_ms;
    uint32_t last_playback_activity_ms;
    uint32_t barge_in_events;
    uint32_t idle_timeouts;
    uint32_t self_echo_rejections;
    uint16_t last_microphone_level_q15;
    uint16_t last_reference_level_q15;
    voice_duplex_event_t last_event;
} voice_duplex_state_t;

/** @brief Initialize a state machine; invalid settings are clamped safely. */
void voice_duplex_core_init(
    voice_duplex_state_t *state,
    const voice_duplex_config_t *config
);

/** @brief Reset per-session counters and timing without changing settings. */
void voice_duplex_core_reset(voice_duplex_state_t *state);

/** @brief Mark the start of an active continuous conversation session. */
void voice_duplex_core_session_started(
    voice_duplex_state_t *state,
    uint32_t now_ms
);

/** @brief Mark the end of an active conversation session. */
void voice_duplex_core_session_ended(
    voice_duplex_state_t *state,
    uint32_t now_ms
);

/**
 * @brief Record that the gateway started speaking.
 *
 * Barge-in is armed only while the assistant/playback path is active, so an
 * ordinary listening frame is forwarded without being classified as an
 * interruption.
 */
void voice_duplex_core_playback_started(
    voice_duplex_state_t *state,
    uint32_t now_ms
);

/** @brief Record that the gateway stopped speaking. */
void voice_duplex_core_playback_stopped(
    voice_duplex_state_t *state,
    uint32_t now_ms
);

/**
 * @brief Evaluate one captured frame.
 *
 * microphone_level_q15 and reference_level_q15 are 0..32767 or 0..1.0 in
 * Q15. A strong signal that appears close to the current reference is treated
 * as the assistant's own playback, not as a child interruption.
 */
voice_duplex_event_t voice_duplex_core_process_frame(
    voice_duplex_state_t *state,
    uint32_t now_ms,
    uint16_t microphone_level_q15,
    uint16_t reference_level_q15,
    bool is_speech
);

/**
 * @brief Advance the continuous-session timer without an audio frame.
 *
 * Returns VOICE_DUPLEX_EVENT_IDLE_TIMEOUT exactly once when an active session
 * has stayed idle for the configured window.
 */
voice_duplex_event_t voice_duplex_core_tick(
    voice_duplex_state_t *state,
    uint32_t now_ms
);

/** @brief Return true when the session is still active. */
bool voice_duplex_core_is_active(const voice_duplex_state_t *state);

#ifdef __cplusplus
}
#endif
