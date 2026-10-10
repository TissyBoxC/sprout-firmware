// Host tests for pure full-duplex, barge-in, and idle-window logic.

#include "voice_duplex_core.h"

#include <cassert>

static voice_duplex_state_t make_state(void) {
    const voice_duplex_config_t config = {
        .activity_threshold_q15 = 2000,
        .echo_guard_threshold_q15 = 2500,
        .barge_in_frames = 2,
        .idle_timeout_ms = 1000,
        .playback_activity_hold_ms = 100,
    };
    voice_duplex_state_t state = {};
    voice_duplex_core_init(&state, &config);
    return state;
}

static void test_inactive_session_ignores_frames(void) {
    voice_duplex_state_t state = make_state();
    assert(voice_duplex_core_process_frame(
        &state,
        10,
        32000,
        0,
        true
    ) == VOICE_DUPLEX_EVENT_NONE);
    assert(!voice_duplex_core_is_active(&state));
}

static void test_playback_requires_one_stable_speech_frame(void) {
    voice_duplex_state_t state = make_state();
    voice_duplex_core_session_started(&state, 0);
    voice_duplex_core_playback_started(&state, 20);

    assert(voice_duplex_core_process_frame(
        &state,
        40,
        12000,
        0,
        true
    ) == VOICE_DUPLEX_EVENT_NONE);
    assert(state.barge_in_events == 0);

    assert(voice_duplex_core_process_frame(
        &state,
        60,
        12000,
        0,
        true
    ) == VOICE_DUPLEX_EVENT_BARGE_IN);
    assert(state.barge_in_events == 1);
}

static void test_echo_like_frame_is_not_a_barge_in(void) {
    voice_duplex_state_t state = make_state();
    voice_duplex_core_session_started(&state, 0);
    voice_duplex_core_playback_started(&state, 20);

    for (uint32_t frame = 0; frame < 6; ++frame) {
        assert(voice_duplex_core_process_frame(
            &state,
            40 + frame * 20,
            12000,
            10500,
            true
        ) == VOICE_DUPLEX_EVENT_NONE);
    }
    assert(state.self_echo_rejections == 6);
    assert(state.barge_in_events == 0);
}

static void test_non_speech_loud_tone_does_not_barge_in(void) {
    voice_duplex_state_t state = make_state();
    voice_duplex_core_session_started(&state, 0);
    voice_duplex_core_playback_started(&state, 20);

    assert(voice_duplex_core_process_frame(
        &state,
        40,
        20000,
        0,
        false
    ) == VOICE_DUPLEX_EVENT_NONE);
    assert(state.barge_in_events == 0);
}

static void test_idle_timeout_fires_once(void) {
    voice_duplex_state_t state = make_state();
    voice_duplex_core_session_started(&state, 100);
    assert(voice_duplex_core_tick(&state, 900) == VOICE_DUPLEX_EVENT_NONE);
    assert(voice_duplex_core_tick(&state, 1100) ==
           VOICE_DUPLEX_EVENT_IDLE_TIMEOUT);
    assert(!voice_duplex_core_is_active(&state));
    assert(voice_duplex_core_tick(&state, 1200) ==
           VOICE_DUPLEX_EVENT_NONE);
}

static void test_speech_refreshes_idle_window(void) {
    voice_duplex_state_t state = make_state();
    voice_duplex_core_session_started(&state, 0);
    assert(voice_duplex_core_process_frame(
        &state,
        800,
        100,
        0,
        true
    ) == VOICE_DUPLEX_EVENT_NONE);
    assert(voice_duplex_core_tick(&state, 1500) == VOICE_DUPLEX_EVENT_NONE);
    assert(voice_duplex_core_tick(&state, 1801) ==
           VOICE_DUPLEX_EVENT_IDLE_TIMEOUT);
}

static void test_playback_hold_prevents_early_idle_timeout(void) {
    voice_duplex_state_t state = make_state();
    voice_duplex_core_session_started(&state, 0);
    voice_duplex_core_playback_started(&state, 900);
    voice_duplex_core_playback_stopped(&state, 950);
    assert(voice_duplex_core_tick(&state, 1050) ==
           VOICE_DUPLEX_EVENT_NONE);
    assert(voice_duplex_core_tick(&state, 2051) ==
           VOICE_DUPLEX_EVENT_IDLE_TIMEOUT);
}

int main(void) {
    test_inactive_session_ignores_frames();
    test_playback_requires_one_stable_speech_frame();
    test_echo_like_frame_is_not_a_barge_in();
    test_non_speech_loud_tone_does_not_barge_in();
    test_idle_timeout_fires_once();
    test_speech_refreshes_idle_window();
    test_playback_hold_prevents_early_idle_timeout();
    return 0;
}
