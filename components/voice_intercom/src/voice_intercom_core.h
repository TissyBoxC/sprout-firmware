#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest guardian call identifier including the terminator. */
#define VOICE_INTERCOM_CALL_ID_SIZE 64

/** Longest call identifier accepted from one command (`data.call_id`). */
#define VOICE_INTERCOM_CALL_ID_INPUT_SIZE 48

/** Longest stable end reason including the terminator. */
#define VOICE_INTERCOM_REASON_SIZE 32

/** Stable call phases exposed to the UI, LED and diagnostics. */
typedef enum {
    VOICE_INTERCOM_PHASE_IDLE = 0,
    VOICE_INTERCOM_PHASE_RINGING,
    VOICE_INTERCOM_PHASE_CONNECTING,
    VOICE_INTERCOM_PHASE_ACTIVE,
} voice_intercom_phase_t;

/** Stable reasons a call ends. Values never change once shipped. */
typedef enum {
    VOICE_INTERCOM_END_NONE = 0,
    VOICE_INTERCOM_END_LOCAL_HANGUP,
    VOICE_INTERCOM_END_REMOTE_HANGUP,
    VOICE_INTERCOM_END_LOCAL_DECLINE,
    VOICE_INTERCOM_END_RING_TIMEOUT,
    VOICE_INTERCOM_END_MAX_DURATION,
    VOICE_INTERCOM_END_CONNECT_FAILED,
    VOICE_INTERCOM_END_TRANSPORT_CLOSED,
} voice_intercom_end_reason_t;

/** Stable result of one call-phase request. */
typedef enum {
    VOICE_INTERCOM_RESULT_OK = 0,
    VOICE_INTERCOM_RESULT_INVALID_STATE,
    VOICE_INTERCOM_RESULT_INVALID_ARGUMENT,
    VOICE_INTERCOM_RESULT_ACCEPT_DISABLED,
} voice_intercom_result_t;

/**
 * @brief Bounded configuration for the call state machine.
 *
 * A zero timeout disables that timer instead of meaning "expire now", so a
 * misparsed setting can never end a call the instant it starts.
 */
typedef struct {
    uint32_t ring_timeout_ms;
    uint32_t max_call_ms;
    /**
     * Initial accept gate. It only ever starts as true for a host test; the
     * production composition root obtains the device's physical-accept
     * confirmation first and then clears the gate on every call end.
     */
    bool accept_enabled;
} voice_intercom_config_t;

/** Bounded state visible to diagnostics, the UI and tests. */
typedef struct {
    voice_intercom_config_t config;
    voice_intercom_phase_t phase;
    voice_intercom_end_reason_t end_reason;
    char call_id[VOICE_INTERCOM_CALL_ID_SIZE];
    /** True only while the realtime audio session may run. */
    bool audio_allowed;
    /** Monotonic millisecond of the current phase start. */
    uint32_t phase_started_ms;
    /** Monotonic millisecond of the future hard end; 0 when none is armed. */
    uint32_t deadline_ms;
    uint32_t ring_rejections;
    uint32_t completed_calls;
} voice_intercom_state_t;

/**
 * @brief Arm a fresh state machine with the resource ledger cleared.
 *
 * A config with zero timeouts keeps both timers disarmed. The accept gate is
 * restricted to the host-test build; in every other build it starts false so
 * no caller can assert an accept that never happened.
 */
void voice_intercom_core_init(
    voice_intercom_state_t *state,
    const voice_intercom_config_t *config
);

/** @brief Return true when a call identifier is non-empty and bounded. */
bool voice_intercom_call_id_is_valid(const char *call_id);

/**
 * @brief Return true when the device truly holds the audio resources.
 *
 * The accept path opens the transport, connects the realtime session and
 * enables the microphone before the call becomes ACTIVE. Passing that ledger
 * here is what makes the accept step mandatory: without an opened transport
 * and an enabled capture lease the call goes straight to CONNECT_FAILED and
 * the state machine never reports `audio_allowed`.
 */
bool voice_intercom_audio_resources_held(
    bool is_transport_open,
    bool is_capture_enabled
);

/**
 * @brief Begin ringing for one guardian call.
 *
 * Rejected while a previous call still holds the audio path, so two guards
 * can never overlap on one device. Nothing is captured while RINGING, and no
 * audio is allowed even by an initial accept gate.
 */
voice_intercom_result_t voice_intercom_core_ring(
    voice_intercom_state_t *state,
    const char *call_id,
    uint32_t now_ms
);

/**
 * @brief Attempt to accept the ringing call.
 *
 * Requires a ringing call, a valid identifier, an enabled accept gate and the
 * real held audio resources. Any failure ends the call as CONNECT_FAILED
 * instead of leaving a half-open session behind.
 */
voice_intercom_result_t voice_intercom_core_accept(
    voice_intercom_state_t *state,
    uint32_t now_ms,
    bool has_audio_resources
);

/**
 * @brief Mark a successfully accepted call as ACTIVE and arm the hard limit.
 *
 * The end deadline is only ever the call since it became active, so the
 * maximum duration cannot be extended by a late accept or a retry.
 */
voice_intercom_result_t voice_intercom_core_connected(
    voice_intercom_state_t *state,
    uint32_t now_ms
);

/** @brief End the call and release the audio path for one stable reason. */
voice_intercom_result_t voice_intercom_core_end(
    voice_intercom_state_t *state,
    voice_intercom_end_reason_t reason,
    uint32_t now_ms
);

/**
 * @brief Advance both timers without an external event.
 *
 * Returns the reason the call ended, or END_NONE while it continues. A
 * wrap-around of the millisecond clock is handled with signed differences.
 */
voice_intercom_end_reason_t voice_intercom_core_tick(
    voice_intercom_state_t *state,
    uint32_t now_ms
);

/** @brief Return true while a call owns the device. */
bool voice_intercom_core_is_busy(const voice_intercom_state_t *state);

/** @brief Return true when the phase is RINGING. */
bool voice_intercom_core_is_ringing(const voice_intercom_state_t *state);

/**
 * @brief Return the smallest remaining timer in whole seconds.
 *
 * Rounds up so a UI countdown never shows zero while the call still runs.
 * Returns 0 when nothing is armed or no call is active.
 */
uint32_t voice_intercom_core_remaining_seconds(
    const voice_intercom_state_t *state,
    uint32_t now_ms
);

/** @brief Return the stable string for one phase. */
const char *voice_intercom_phase_name(voice_intercom_phase_t phase);

/** @brief Return the stable string for one end reason. */
const char *voice_intercom_end_reason_name(voice_intercom_end_reason_t reason);

/** @brief Return the stable string for one call result. */
const char *voice_intercom_result_name(voice_intercom_result_t result);

#ifdef __cplusplus
}
#endif
