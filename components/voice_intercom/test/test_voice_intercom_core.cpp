// Host tests for the pure voice intercom call state machine. The state
// machine is the only authority that can report a call as able to carry
// audio, so these cases pin the child-safety contract directly.

#include <cstdio>
#include <cstring>

#include "voice_intercom_core.h"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

voice_intercom_config_t make_config(void) {
    voice_intercom_config_t config = {};
    config.ring_timeout_ms = 1000;
    config.max_call_ms = 5000;
    config.accept_enabled = true;
    return config;
}

void initialize(voice_intercom_state_t *state) {
    // Tests start with the gate open so each case can reach the accept path;
    // the runtime tests cover the production gate that starts closed.
    const voice_intercom_config_t config = make_config();
    voice_intercom_core_init(state, &config);
}

void ring_and_accept(voice_intercom_state_t *state) {
    initialize(state);
    check(
        voice_intercom_core_ring(state, "call-1", 100) ==
            VOICE_INTERCOM_RESULT_OK,
        "ringing must start for a valid call"
    );
    check(
        voice_intercom_core_accept(state, 200, true) ==
            VOICE_INTERCOM_RESULT_OK,
        "accept must succeed with real audio resources"
    );
    check(
        voice_intercom_core_connected(state, 300) ==
            VOICE_INTERCOM_RESULT_OK,
        "connect must activate the accepted call"
    );
}

void test_idle_allows_no_audio(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    check(!state.audio_allowed, "an idle device never allows audio");
    check(
        state.phase == VOICE_INTERCOM_PHASE_IDLE,
        "an idle device reports the idle phase"
    );
    check(
        voice_intercom_core_accept(&state, 0, true) ==
            VOICE_INTERCOM_RESULT_INVALID_STATE,
        "accept without a ringing call must be refused"
    );
    check(!state.audio_allowed, "a refused accept still allows no audio");
}

void test_ringing_never_allows_audio(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    check(
        voice_intercom_core_ring(&state, "call-1", 10) ==
            VOICE_INTERCOM_RESULT_OK,
        "a valid call starts ringing"
    );
    check(state.phase == VOICE_INTERCOM_PHASE_RINGING, "phase is ringing");
    check(!state.audio_allowed, "ringing never allows audio");
    check(
        voice_intercom_core_is_ringing(&state),
        "the ringing predicate matches the phase"
    );
    check(
        voice_intercom_core_connected(&state, 20) ==
            VOICE_INTERCOM_RESULT_INVALID_STATE,
        "a call cannot become active without accept"
    );
    check(!state.audio_allowed, "a skipped accept still allows no audio");
}

void test_accept_requires_real_audio_resources(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    (void)voice_intercom_core_ring(&state, "call-1", 10);
    check(
        voice_intercom_core_accept(&state, 20, false) ==
            VOICE_INTERCOM_RESULT_INVALID_STATE,
        "accept without the audio resources must be refused"
    );
    check(
        state.phase == VOICE_INTERCOM_PHASE_IDLE,
        "a failed accept releases the call"
    );
    check(
        state.end_reason == VOICE_INTERCOM_END_CONNECT_FAILED,
        "a failed accept reports connect_failed"
    );
    check(!state.audio_allowed, "a failed accept allows no audio");
    check(state.ring_rejections == 1, "the refusal is counted");
}

void test_accept_without_physical_gate_is_refused(void) {
    voice_intercom_config_t config = make_config();
    config.accept_enabled = false;
    voice_intercom_state_t state = {};
    voice_intercom_core_init(&state, &config);
    (void)voice_intercom_core_ring(&state, "call-1", 10);
    check(
        voice_intercom_core_accept(&state, 20, true) ==
            VOICE_INTERCOM_RESULT_ACCEPT_DISABLED,
        "a closed accept gate refuses the accept even with resources"
    );
    check(
        state.phase == VOICE_INTERCOM_PHASE_RINGING,
        "a refused accept keeps the call ringing"
    );
    check(!state.audio_allowed, "a closed gate never allows audio");
}

void test_resources_require_transport_and_capture(void) {
    check(
        voice_intercom_audio_resources_held(true, true),
        "transport plus capture counts as held"
    );
    check(
        !voice_intercom_audio_resources_held(false, true),
        "capture without a transport is not a held resource set"
    );
    check(
        !voice_intercom_audio_resources_held(true, false),
        "a transport without capture is not a held resource set"
    );
}

void test_active_call_arms_the_duration_limit(void) {
    voice_intercom_state_t state = {};
    ring_and_accept(&state);
    check(
        state.phase == VOICE_INTERCOM_PHASE_ACTIVE,
        "an accepted call is active"
    );
    check(state.audio_allowed, "only an active call allows audio");
    check(
        state.deadline_ms == 300 + 5000,
        "the hard limit starts when the call became active"
    );
    check(
        voice_intercom_core_tick(&state, 300 + 4999) ==
            VOICE_INTERCOM_END_NONE,
        "the call continues before the hard limit"
    );
    check(
        voice_intercom_core_tick(&state, 300 + 5000) ==
            VOICE_INTERCOM_END_MAX_DURATION,
        "the hard limit ends the call on the device"
    );
    check(
        state.phase == VOICE_INTERCOM_PHASE_IDLE,
        "the call is released after the hard limit"
    );
    check(!state.audio_allowed, "the hard limit releases audio");
    check(state.completed_calls == 1, "the call is counted as completed");
}

void test_late_accept_cannot_extend_the_limit(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    (void)voice_intercom_core_ring(&state, "call-1", 0);
    // The accept arrives at the last millisecond of the ring window; the
    // duration limit must still be measured from the active moment only.
    check(
        voice_intercom_core_accept(&state, 999, true) ==
            VOICE_INTERCOM_RESULT_OK,
        "accept inside the window succeeds"
    );
    check(
        voice_intercom_core_connected(&state, 1000) ==
            VOICE_INTERCOM_RESULT_OK,
        "connect completes"
    );
    check(
        state.deadline_ms == 1000 + 5000,
        "the limit is anchored at the active moment"
    );
}

void test_ring_timeout_declines_the_call(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    (void)voice_intercom_core_ring(&state, "call-1", 10);
    check(
        voice_intercom_core_tick(&state, 10 + 999) == VOICE_INTERCOM_END_NONE,
        "the ring continues before the timeout"
    );
    check(
        voice_intercom_core_tick(&state, 10 + 1000) ==
            VOICE_INTERCOM_END_RING_TIMEOUT,
        "an unanswered call ends at the ring timeout"
    );
    check(
        state.end_reason == VOICE_INTERCOM_END_RING_TIMEOUT,
        "the end reason is ring_timeout"
    );
    check(
        state.phase == VOICE_INTERCOM_PHASE_IDLE,
        "the timed out call is released"
    );
    check(
        state.completed_calls == 0,
        "a timed out call is not a completed call"
    );
    check(
        voice_intercom_core_tick(&state, 10 + 9000) ==
            VOICE_INTERCOM_END_NONE,
        "the timeout is reported once"
    );
}

void test_connect_stall_ends_the_call(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    (void)voice_intercom_core_ring(&state, "call-1", 0);
    (void)voice_intercom_core_accept(&state, 10, true);
    check(
        state.phase == VOICE_INTERCOM_PHASE_CONNECTING,
        "an accepted call waits for the session to connect"
    );
    check(
        voice_intercom_core_tick(&state, 10 + 1000) ==
            VOICE_INTERCOM_END_RING_TIMEOUT,
        "a stalled connect is bounded by the same guardian window"
    );
    check(!state.audio_allowed, "a stalled connect releases audio");
}

void test_decline_and_hangup_end_the_call(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    (void)voice_intercom_core_ring(&state, "call-1", 0);
    check(
        voice_intercom_core_end(
            &state,
            VOICE_INTERCOM_END_LOCAL_DECLINE,
            10
        ) == VOICE_INTERCOM_RESULT_OK,
        "declining a ringing call succeeds"
    );
    check(
        state.end_reason == VOICE_INTERCOM_END_LOCAL_DECLINE,
        "the decline reason is recorded"
    );
    check(
        voice_intercom_core_end(
            &state,
            VOICE_INTERCOM_END_LOCAL_HANGUP,
            20
        ) == VOICE_INTERCOM_RESULT_INVALID_STATE,
        "a call without a phase cannot end twice"
    );

    ring_and_accept(&state);
    check(
        voice_intercom_core_end(
            &state,
            VOICE_INTERCOM_END_REMOTE_HANGUP,
            400
        ) == VOICE_INTERCOM_RESULT_OK,
        "a guardian hangup ends an active call"
    );
    check(
        state.end_reason == VOICE_INTERCOM_END_REMOTE_HANGUP,
        "the guardian hangup reason is recorded"
    );
    check(!state.audio_allowed, "a remote hangup releases audio");
    check(
        voice_intercom_core_tick(&state, 100000) == VOICE_INTERCOM_END_NONE,
        "an ended call has no armed timer"
    );
}

void test_overlapping_calls_are_refused(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    (void)voice_intercom_core_ring(&state, "call-1", 0);
    check(
        voice_intercom_core_ring(&state, "call-2", 10) ==
            VOICE_INTERCOM_RESULT_INVALID_STATE,
        "a second call cannot ring while one is ringing"
    );
    check(
        strcmp(state.call_id, "call-1") == 0,
        "the first call keeps its identifier"
    );
    ring_and_accept(&state);
    check(
        voice_intercom_core_ring(&state, "call-3", 500) ==
            VOICE_INTERCOM_RESULT_INVALID_STATE,
        "a second call cannot ring while one is active"
    );
}

void test_call_identifiers_are_bounded(void) {
    check(voice_intercom_call_id_is_valid("call-1"), "a simple id is valid");
    check(!voice_intercom_call_id_is_valid(""), "an empty id is invalid");
    check(
        !voice_intercom_call_id_is_valid(nullptr),
        "a missing id is invalid"
    );
    check(
        !voice_intercom_call_id_is_valid("call 1"),
        "an id with a space is invalid"
    );
    check(
        !voice_intercom_call_id_is_valid("call/1"),
        "an id with a path separator is invalid"
    );
    char long_id[VOICE_INTERCOM_CALL_ID_INPUT_SIZE + 2] = {};
    std::memset(long_id, 'a', sizeof(long_id) - 1);
    check(!voice_intercom_call_id_is_valid(long_id), "a long id is invalid");

    voice_intercom_state_t state = {};
    initialize(&state);
    check(
        voice_intercom_core_ring(&state, "bad id", 0) ==
            VOICE_INTERCOM_RESULT_INVALID_ARGUMENT,
        "ringing refuses an invalid identifier"
    );
    check(
        state.phase == VOICE_INTERCOM_PHASE_IDLE,
        "an invalid identifier never starts a call"
    );
}

void test_countdown_rounds_up(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    (void)voice_intercom_core_ring(&state, "call-1", 0);
    check(
        voice_intercom_core_remaining_seconds(&state, 0) == 1,
        "a full second window counts as one second"
    );
    check(
        voice_intercom_core_remaining_seconds(&state, 1) == 1,
        "a partial second rounds up"
    );
    (void)voice_intercom_core_tick(&state, 1000);
    check(
        voice_intercom_core_remaining_seconds(&state, 0) == 0,
        "an ended call has no countdown"
    );
}

void test_clock_wrap_keeps_timers_correct(void) {
    voice_intercom_state_t state = {};
    initialize(&state);
    const uint32_t start = 0xFFFFFF00u;
    (void)voice_intercom_core_ring(&state, "call-1", start);
    check(
        state.deadline_ms == start + 1000,
        "the deadline crosses the wrap boundary"
    );
    check(
        voice_intercom_core_tick(&state, start + 999) ==
            VOICE_INTERCOM_END_NONE,
        "the ring continues across the wrap"
    );
    check(
        voice_intercom_core_tick(&state, start + 1000) ==
            VOICE_INTERCOM_END_RING_TIMEOUT,
        "the timeout fires across the wrap"
    );
}

void test_default_config_still_bounds_the_call(void) {
    voice_intercom_state_t state = {};
    voice_intercom_core_init(&state, nullptr);
    check(
        state.config.ring_timeout_ms == 30000,
        "a missing config keeps the 30 second ring bound"
    );
    check(
        state.config.max_call_ms == 300000,
        "a missing config keeps the 5 minute call bound"
    );
}

void test_name_contracts(void) {
    check(
        std::strcmp(voice_intercom_phase_name(VOICE_INTERCOM_PHASE_RINGING), "ringing") == 0,
        "the ringing phase name is stable"
    );
    check(
        std::strcmp(
            voice_intercom_end_reason_name(VOICE_INTERCOM_END_MAX_DURATION),
            "max_duration"
        ) == 0,
        "the duration limit name is stable"
    );
    check(
        std::strcmp(
            voice_intercom_result_name(VOICE_INTERCOM_RESULT_ACCEPT_DISABLED),
            "accept_disabled"
        ) == 0,
        "the closed gate result name is stable"
    );
}

}  // namespace

int main(void) {
    test_idle_allows_no_audio();
    test_ringing_never_allows_audio();
    test_accept_requires_real_audio_resources();
    test_accept_without_physical_gate_is_refused();
    test_resources_require_transport_and_capture();
    test_active_call_arms_the_duration_limit();
    test_late_accept_cannot_extend_the_limit();
    test_ring_timeout_declines_the_call();
    test_connect_stall_ends_the_call();
    test_decline_and_hangup_end_the_call();
    test_overlapping_calls_are_refused();
    test_call_identifiers_are_bounded();
    test_countdown_rounds_up();
    test_clock_wrap_keeps_timers_correct();
    test_default_config_still_bounds_the_call();
    test_name_contracts();
    if (failures != 0) {
        std::printf("%d core assertions failed\n", failures);
        return 1;
    }
    std::printf("voice_intercom_core: all assertions passed\n");
    return 0;
}
