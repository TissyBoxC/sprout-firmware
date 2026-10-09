// Host test for the pure privacy guard state.

#include "privacy_guard_core.h"

#include <cassert>

static void test_defaults_are_deny_all(void) {
    privacy_guard_core_state_t state;
    privacy_guard_core_init(&state);
    assert(privacy_guard_core_state_is_compatible(&state));
    assert(!privacy_guard_core_is_allowed(
        &state,
        PRIVACY_GUARD_DATA_AUDIO_UPLOAD
    ));
    assert(!privacy_guard_core_is_allowed(
        &state,
        PRIVACY_GUARD_DATA_IMAGE_UPLOAD
    ));
    assert(!privacy_guard_core_is_allowed(
        &state,
        PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
    ));
    assert(!privacy_guard_core_is_allowed(
        &state,
        PRIVACY_GUARD_DATA_USAGE_ANALYTICS
    ));
}

static void test_rejects_incompatible_and_invalid_input(void) {
    privacy_guard_core_state_t state;
    privacy_guard_core_init(&state);
    assert(!privacy_guard_core_is_allowed(nullptr, PRIVACY_GUARD_DATA_AUDIO_UPLOAD));
    assert(!privacy_guard_core_is_allowed(
        &state,
        (privacy_guard_data_class_t)99
    ));
    state.state_version = 999;
    assert(!privacy_guard_core_state_is_compatible(&state));
}

static void test_consent_flags_are_authoritative(void) {
    privacy_guard_core_state_t state;
    privacy_guard_core_init(&state);
    state.consent[PRIVACY_GUARD_DATA_AUDIO_UPLOAD] = true;
    assert(privacy_guard_core_is_allowed(
        &state,
        PRIVACY_GUARD_DATA_AUDIO_UPLOAD
    ));
    state.consent[PRIVACY_GUARD_DATA_AUDIO_UPLOAD] = false;
    assert(!privacy_guard_core_is_allowed(
        &state,
        PRIVACY_GUARD_DATA_AUDIO_UPLOAD
    ));
}

int main(void) {
    test_defaults_are_deny_all();
    test_rejects_incompatible_and_invalid_input();
    test_consent_flags_are_authoritative();
    return 0;
}
