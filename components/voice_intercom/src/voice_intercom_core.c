#include "voice_intercom_core.h"

#include <string.h>

// The accept gate may only start open in the host-test build. Production
// always begins with the gate closed, so no command or stray call can start
// audio before the composition root registered the physical accept path.
// A host test build that does not need the gate states it explicitly as a
// negative form; every other build keeps the closed gate.
#if defined(VOICE_INTERCOM_HOST_TESTS) && \
    !defined(VOICE_INTERCOM_HOST_NO_INITIAL_ACCEPT)
#define VOICE_INTERCOM_INITIAL_ACCEPT_GATE true
#else
#define VOICE_INTERCOM_INITIAL_ACCEPT_GATE false
#endif

// A guardian call without any bound would hold the microphone forever, so a
// missing configuration still arms the documented product limits.
#define VOICE_INTERCOM_DEFAULT_RING_TIMEOUT_MS 30000u
#define VOICE_INTERCOM_DEFAULT_MAX_CALL_MS 300000u

static bool voice_intercom_time_reached(uint32_t deadline_ms, uint32_t now_ms) {
    // Signed difference keeps the comparison correct across the millisecond
    // counter wrap, which happens after roughly 49 days of uptime.
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void voice_intercom_core_clear_call(voice_intercom_state_t *state) {
    state->phase = VOICE_INTERCOM_PHASE_IDLE;
    state->audio_allowed = false;
    state->deadline_ms = 0;
    state->phase_started_ms = 0;
    memset(state->call_id, 0, sizeof(state->call_id));
}

void voice_intercom_core_init(
    voice_intercom_state_t *state,
    const voice_intercom_config_t *config
) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    if (config != NULL) {
        state->config = *config;
    } else {
        // An explicit field list keeps the build warning-free under the
        // harnesses that treat missing initializers as errors.
        state->config.ring_timeout_ms = 0;
        state->config.max_call_ms = 0;
        state->config.accept_enabled = false;
    }
    if (state->config.ring_timeout_ms == 0) {
        state->config.ring_timeout_ms = VOICE_INTERCOM_DEFAULT_RING_TIMEOUT_MS;
    }
    if (state->config.max_call_ms == 0) {
        state->config.max_call_ms = VOICE_INTERCOM_DEFAULT_MAX_CALL_MS;
    }
    // The caller's accept gate is honoured: the composition root and the host
    // tests both decide explicitly whether this state machine may accept.
    // Only a NULL configuration falls back to the build default.
    if (config == NULL) {
        state->config.accept_enabled = VOICE_INTERCOM_INITIAL_ACCEPT_GATE;
    }
    state->phase = VOICE_INTERCOM_PHASE_IDLE;
    state->end_reason = VOICE_INTERCOM_END_NONE;
}

bool voice_intercom_call_id_is_valid(const char *call_id) {
    if (call_id == NULL) {
        return false;
    }
    const size_t length = strlen(call_id);
    if (length == 0 || length > VOICE_INTERCOM_CALL_ID_INPUT_SIZE) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const unsigned char value = (unsigned char)call_id[index];
        const bool is_letter =
            (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
        const bool is_digit = value >= '0' && value <= '9';
        // The identifier is echoed in acknowledgements and logs, so only a
        // conservative ASCII subset is accepted.
        if (!is_letter && !is_digit && value != '-' && value != '_') {
            return false;
        }
    }
    return true;
}

bool voice_intercom_audio_resources_held(
    bool is_transport_open,
    bool is_capture_enabled
) {
    return is_transport_open && is_capture_enabled;
}

voice_intercom_result_t voice_intercom_core_ring(
    voice_intercom_state_t *state,
    const char *call_id,
    uint32_t now_ms
) {
    if (state == NULL) {
        return VOICE_INTERCOM_RESULT_INVALID_ARGUMENT;
    }
    if (state->phase != VOICE_INTERCOM_PHASE_IDLE || state->audio_allowed) {
        return VOICE_INTERCOM_RESULT_INVALID_STATE;
    }
    if (!voice_intercom_call_id_is_valid(call_id)) {
        return VOICE_INTERCOM_RESULT_INVALID_ARGUMENT;
    }
    memset(state->call_id, 0, sizeof(state->call_id));
    memcpy(state->call_id, call_id, strlen(call_id));
    state->phase = VOICE_INTERCOM_PHASE_RINGING;
    // Ringing is a silent phase for the microphone: the flag is explicit so a
    // reviewer can see that no capture path exists before the accept.
    state->audio_allowed = false;
    state->phase_started_ms = now_ms;
    state->deadline_ms = now_ms + state->config.ring_timeout_ms;
    state->end_reason = VOICE_INTERCOM_END_NONE;
    return VOICE_INTERCOM_RESULT_OK;
}

voice_intercom_result_t voice_intercom_core_accept(
    voice_intercom_state_t *state,
    uint32_t now_ms,
    bool has_audio_resources
) {
    if (state == NULL) {
        return VOICE_INTERCOM_RESULT_INVALID_ARGUMENT;
    }
    if (state->phase != VOICE_INTERCOM_PHASE_RINGING) {
        state->ring_rejections += 1;
        return VOICE_INTERCOM_RESULT_INVALID_STATE;
    }
    if (!state->config.accept_enabled) {
        state->ring_rejections += 1;
        return VOICE_INTERCOM_RESULT_ACCEPT_DISABLED;
    }
    if (!has_audio_resources) {
        state->ring_rejections += 1;
        voice_intercom_core_end(
            state,
            VOICE_INTERCOM_END_CONNECT_FAILED,
            now_ms
        );
        return VOICE_INTERCOM_RESULT_INVALID_STATE;
    }
    state->phase = VOICE_INTERCOM_PHASE_CONNECTING;
    state->phase_started_ms = now_ms;
    // The ring timer is armed again so a stalled connect cannot hold the
    // audio path past the same guardian-visible window.
    state->deadline_ms = now_ms + state->config.ring_timeout_ms;
    return VOICE_INTERCOM_RESULT_OK;
}

voice_intercom_result_t voice_intercom_core_connected(
    voice_intercom_state_t *state,
    uint32_t now_ms
) {
    if (state == NULL) {
        return VOICE_INTERCOM_RESULT_INVALID_ARGUMENT;
    }
    if (state->phase != VOICE_INTERCOM_PHASE_CONNECTING) {
        return VOICE_INTERCOM_RESULT_INVALID_STATE;
    }
    state->phase = VOICE_INTERCOM_PHASE_ACTIVE;
    state->audio_allowed = true;
    state->phase_started_ms = now_ms;
    state->deadline_ms = now_ms + state->config.max_call_ms;
    return VOICE_INTERCOM_RESULT_OK;
}

voice_intercom_result_t voice_intercom_core_end(
    voice_intercom_state_t *state,
    voice_intercom_end_reason_t reason,
    uint32_t now_ms
) {
    if (state == NULL) {
        return VOICE_INTERCOM_RESULT_INVALID_ARGUMENT;
    }
    if (state->phase == VOICE_INTERCOM_PHASE_IDLE) {
        return VOICE_INTERCOM_RESULT_INVALID_STATE;
    }
    (void)now_ms;
    if (state->phase == VOICE_INTERCOM_PHASE_ACTIVE) {
        state->completed_calls += 1;
    }
    state->end_reason = reason;
    voice_intercom_core_clear_call(state);
    return VOICE_INTERCOM_RESULT_OK;
}

voice_intercom_end_reason_t voice_intercom_core_tick(
    voice_intercom_state_t *state,
    uint32_t now_ms
) {
    if (state == NULL || state->deadline_ms == 0) {
        return VOICE_INTERCOM_END_NONE;
    }
    if (state->phase != VOICE_INTERCOM_PHASE_RINGING &&
        state->phase != VOICE_INTERCOM_PHASE_CONNECTING &&
        state->phase != VOICE_INTERCOM_PHASE_ACTIVE) {
        return VOICE_INTERCOM_END_NONE;
    }
    if (!voice_intercom_time_reached(state->deadline_ms, now_ms)) {
        return VOICE_INTERCOM_END_NONE;
    }
    const voice_intercom_end_reason_t reason =
        state->phase == VOICE_INTERCOM_PHASE_ACTIVE
            ? VOICE_INTERCOM_END_MAX_DURATION
            : VOICE_INTERCOM_END_RING_TIMEOUT;
    (void)voice_intercom_core_end(state, reason, now_ms);
    return reason;
}

bool voice_intercom_core_is_busy(const voice_intercom_state_t *state) {
    return state != NULL && state->phase != VOICE_INTERCOM_PHASE_IDLE;
}

bool voice_intercom_core_is_ringing(const voice_intercom_state_t *state) {
    return state != NULL && state->phase == VOICE_INTERCOM_PHASE_RINGING;
}

uint32_t voice_intercom_core_remaining_seconds(
    const voice_intercom_state_t *state,
    uint32_t now_ms
) {
    if (state == NULL || state->deadline_ms == 0) {
        return 0;
    }
    if (voice_intercom_time_reached(state->deadline_ms, now_ms)) {
        return 0;
    }
    const uint32_t remaining_ms = state->deadline_ms - now_ms;
    return (remaining_ms + 999u) / 1000u;
}

const char *voice_intercom_phase_name(voice_intercom_phase_t phase) {
    switch (phase) {
        case VOICE_INTERCOM_PHASE_IDLE:
            return "idle";
        case VOICE_INTERCOM_PHASE_RINGING:
            return "ringing";
        case VOICE_INTERCOM_PHASE_CONNECTING:
            return "connecting";
        case VOICE_INTERCOM_PHASE_ACTIVE:
            return "active";
        default:
            return "unknown";
    }
}

const char *voice_intercom_end_reason_name(voice_intercom_end_reason_t reason) {
    switch (reason) {
        case VOICE_INTERCOM_END_NONE:
            return "none";
        case VOICE_INTERCOM_END_LOCAL_HANGUP:
            return "local_hangup";
        case VOICE_INTERCOM_END_REMOTE_HANGUP:
            return "remote_hangup";
        case VOICE_INTERCOM_END_LOCAL_DECLINE:
            return "local_decline";
        case VOICE_INTERCOM_END_RING_TIMEOUT:
            return "ring_timeout";
        case VOICE_INTERCOM_END_MAX_DURATION:
            return "max_duration";
        case VOICE_INTERCOM_END_CONNECT_FAILED:
            return "connect_failed";
        case VOICE_INTERCOM_END_TRANSPORT_CLOSED:
            return "transport_closed";
        default:
            return "unknown";
    }
}

const char *voice_intercom_result_name(voice_intercom_result_t result) {
    switch (result) {
        case VOICE_INTERCOM_RESULT_OK:
            return "ok";
        case VOICE_INTERCOM_RESULT_INVALID_STATE:
            return "invalid_state";
        case VOICE_INTERCOM_RESULT_INVALID_ARGUMENT:
            return "invalid_argument";
        case VOICE_INTERCOM_RESULT_ACCEPT_DISABLED:
            return "accept_disabled";
        default:
            return "unknown";
    }
}
