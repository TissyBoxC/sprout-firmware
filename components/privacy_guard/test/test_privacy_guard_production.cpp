// Host test for the production privacy guard wiring.

#include "privacy_guard.h"

#include <cassert>

namespace {

int clear_calls;
privacy_guard_action_t last_action;

esp_err_t recording_clear(privacy_guard_action_t action) {
    ++clear_calls;
    last_action = action;
    return ESP_OK;
}

void test_unregistered_handler_fails_instead_of_faking_success(void) {
    // privacy_guard_init wires the NVS-backed consent storage but no real cache
    // handler, so a clear must fail closed instead of claiming a deletion.
    assert(privacy_guard_init() == ESP_OK);
    assert(privacy_guard_is_ready());
    assert(privacy_guard_clear_local_data(PRIVACY_GUARD_ACTION_UNBIND) ==
           PRIVACY_GUARD_ERR_CACHE_BUSY);
    assert(privacy_guard_local_deletion_count() == 0);

    // A NULL handler is rejected so the guard cannot be disarmed by accident.
    assert(privacy_guard_set_clear_cache_handler(nullptr) ==
           ESP_ERR_INVALID_ARG);
}

void test_registered_handler_runs_the_real_deletion(void) {
    assert(privacy_guard_set_clear_cache_handler(recording_clear) == ESP_OK);
    assert(privacy_guard_clear_local_data(
               PRIVACY_GUARD_ACTION_GUARDIAN_REQUEST
           ) == PRIVACY_GUARD_OK);
    assert(clear_calls == 1);
    assert(last_action == PRIVACY_GUARD_ACTION_GUARDIAN_REQUEST);
    assert(privacy_guard_local_deletion_count() == 1);
}

void test_apply_consent_updates_every_class(void) {
    assert(privacy_guard_apply_consent(true, false, true, false) ==
           PRIVACY_GUARD_OK);
    assert(privacy_guard_has_consent(PRIVACY_GUARD_DATA_AUDIO_UPLOAD));
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_IMAGE_UPLOAD));
    assert(privacy_guard_has_consent(
        PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
    ));
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_USAGE_ANALYTICS));

    assert(privacy_guard_apply_consent(false, false, false, false) ==
           PRIVACY_GUARD_OK);
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_AUDIO_UPLOAD));
    assert(!privacy_guard_has_consent(
        PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
    ));
}

}  // namespace

int main() {
    test_unregistered_handler_fails_instead_of_faking_success();
    test_registered_handler_runs_the_real_deletion();
    test_apply_consent_updates_every_class();
    return 0;
}
