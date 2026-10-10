#include "voice_duplex_core.h"

#include <stddef.h>

#define VOICE_DUPLEX_MAX_BARGE_IN_FRAMES 100u

static uint32_t voice_duplex_abs_difference_u32(uint32_t left, uint32_t right) {
    return left >= right ? left - right : right - left;
}

static uint16_t voice_duplex_clamp_q15(uint32_t value) {
    return value > 32767u ? 32767u : (uint16_t)value;
}

static void voice_duplex_core_reset_activity(voice_duplex_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->consecutive_activity_frames = 0;
    state->last_microphone_level_q15 = 0;
    state->last_reference_level_q15 = 0;
}

void voice_duplex_core_init(
    voice_duplex_state_t *state,
    const voice_duplex_config_t *config
) {
    if (state == NULL) {
        return;
    }
    if (config != NULL) {
        state->config = *config;
    } else {
        state->config = (voice_duplex_config_t){
            .activity_threshold_q15 = 0,
            .echo_guard_threshold_q15 = 0,
            .barge_in_frames = 0,
            .idle_timeout_ms = 0,
            .playback_activity_hold_ms = 0,
        };
    }
    if (state->config.activity_threshold_q15 == 0) {
        state->config.activity_threshold_q15 = 1200;
    }
    if (state->config.echo_guard_threshold_q15 == 0) {
        state->config.echo_guard_threshold_q15 = 1800;
    }
    if (state->config.barge_in_frames == 0 ||
        state->config.barge_in_frames > VOICE_DUPLEX_MAX_BARGE_IN_FRAMES) {
        state->config.barge_in_frames = 2;
    }
    if (state->config.idle_timeout_ms == 0) {
        state->config.idle_timeout_ms = 12000;
    }
    if (state->config.playback_activity_hold_ms == 0) {
        state->config.playback_activity_hold_ms = 300;
    }
    state->active = false;
    state->playback_active = false;
    state->barge_in_armed = false;
    state->last_activity_ms = 0;
    state->last_playback_activity_ms = 0;
    state->barge_in_events = 0;
    state->idle_timeouts = 0;
    state->self_echo_rejections = 0;
    state->last_event = VOICE_DUPLEX_EVENT_NONE;
    voice_duplex_core_reset_activity(state);
}

void voice_duplex_core_reset(voice_duplex_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->active = false;
    state->playback_active = false;
    state->barge_in_armed = false;
    state->last_activity_ms = 0;
    state->last_playback_activity_ms = 0;
    state->last_event = VOICE_DUPLEX_EVENT_NONE;
    voice_duplex_core_reset_activity(state);
}

void voice_duplex_core_session_started(
    voice_duplex_state_t *state,
    uint32_t now_ms
) {
    if (state == NULL) {
        return;
    }
    state->active = true;
    state->playback_active = false;
    state->barge_in_armed = false;
    state->last_activity_ms = now_ms;
    state->last_playback_activity_ms = now_ms;
    voice_duplex_core_reset_activity(state);
}

void voice_duplex_core_session_ended(
    voice_duplex_state_t *state,
    uint32_t now_ms
) {
    if (state == NULL) {
        return;
    }
    (void)now_ms;
    state->active = false;
    state->playback_active = false;
    state->barge_in_armed = false;
    voice_duplex_core_reset_activity(state);
}

void voice_duplex_core_playback_started(
    voice_duplex_state_t *state,
    uint32_t now_ms
) {
    if (state == NULL) {
        return;
    }
    state->playback_active = true;
    state->barge_in_armed = state->active;
    state->last_playback_activity_ms = now_ms;
    state->last_activity_ms = now_ms;
    state->consecutive_activity_frames = 0;
}

void voice_duplex_core_playback_stopped(
    voice_duplex_state_t *state,
    uint32_t now_ms
) {
    if (state == NULL) {
        return;
    }
    state->playback_active = false;
    state->barge_in_armed = false;
    state->last_playback_activity_ms = now_ms;
    state->last_activity_ms = now_ms;
    state->consecutive_activity_frames = 0;
}

voice_duplex_event_t voice_duplex_core_process_frame(
    voice_duplex_state_t *state,
    uint32_t now_ms,
    uint16_t microphone_level_q15,
    uint16_t reference_level_q15,
    bool is_speech
) {
    if (state == NULL) {
        return VOICE_DUPLEX_EVENT_NONE;
    }

    state->last_microphone_level_q15 =
        voice_duplex_clamp_q15(microphone_level_q15);
    state->last_reference_level_q15 =
        voice_duplex_clamp_q15(reference_level_q15);
    state->last_event = VOICE_DUPLEX_EVENT_NONE;

    if (is_speech || microphone_level_q15 >= state->config.activity_threshold_q15) {
        state->last_activity_ms = now_ms;
    }

    if (!state->active || !state->barge_in_armed || !is_speech) {
        state->consecutive_activity_frames = 0;
        return VOICE_DUPLEX_EVENT_NONE;
    }

    const uint32_t echo_delta = voice_duplex_abs_difference_u32(
        microphone_level_q15,
        reference_level_q15
    );
    const bool reference_is_present =
        reference_level_q15 >= state->config.echo_guard_threshold_q15;
    const bool close_to_reference =
        reference_is_present &&
        echo_delta < state->config.echo_guard_threshold_q15;

    if (reference_is_present && close_to_reference) {
        state->self_echo_rejections++;
        state->consecutive_activity_frames = 0;
        return VOICE_DUPLEX_EVENT_NONE;
    }

    state->last_playback_activity_ms = now_ms;
    state->consecutive_activity_frames++;
    if (state->consecutive_activity_frames < state->config.barge_in_frames) {
        return VOICE_DUPLEX_EVENT_NONE;
    }

    state->consecutive_activity_frames = 0;
    state->barge_in_armed = false;
    state->barge_in_events++;
    state->last_event = VOICE_DUPLEX_EVENT_BARGE_IN;
    return VOICE_DUPLEX_EVENT_BARGE_IN;
}

voice_duplex_event_t voice_duplex_core_tick(
    voice_duplex_state_t *state,
    uint32_t now_ms
) {
    if (state == NULL || !state->active || state->config.idle_timeout_ms == 0) {
        return VOICE_DUPLEX_EVENT_NONE;
    }

    if (now_ms - state->last_playback_activity_ms <
        state->config.playback_activity_hold_ms) {
        // The output path can be between frames while the speaker is still
        // draining. Keep the session active during that bounded hold so a
        // late echo frame cannot be interpreted as inactivity.
        state->last_activity_ms = now_ms;
    }

    const uint32_t idle_for = now_ms - state->last_activity_ms;
    if (idle_for < state->config.idle_timeout_ms) {
        return VOICE_DUPLEX_EVENT_NONE;
    }

    state->active = false;
    state->playback_active = false;
    state->barge_in_armed = false;
    state->idle_timeouts++;
    state->last_event = VOICE_DUPLEX_EVENT_IDLE_TIMEOUT;
    return VOICE_DUPLEX_EVENT_IDLE_TIMEOUT;
}

bool voice_duplex_core_is_active(const voice_duplex_state_t *state) {
    return state != NULL && state->active;
}
