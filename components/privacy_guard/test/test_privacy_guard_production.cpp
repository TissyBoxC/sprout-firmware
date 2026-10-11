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

void test_sync_consent_erases_only_on_revocation(void) {
    // A grant must not erase data; only a transition from granted to revoked
    // is destructive, so a pure grant path must leave the cache handler idle.
    assert(privacy_guard_apply_consent(false, false, false, false) ==
           PRIVACY_GUARD_OK);
    clear_calls = 0;
    bool revoked = true;
    assert(privacy_guard_sync_consent(true, true, true, true, &revoked) ==
           PRIVACY_GUARD_OK);
    assert(!revoked);
    assert(clear_calls == 0);
    assert(privacy_guard_has_consent(PRIVACY_GUARD_DATA_AUDIO_UPLOAD));
    assert(privacy_guard_has_consent(PRIVACY_GUARD_DATA_IMAGE_UPLOAD));
    assert(privacy_guard_has_consent(
        PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
    ));
    assert(privacy_guard_has_consent(PRIVACY_GUARD_DATA_USAGE_ANALYTICS));

    // Withdrawing one class erases the retained local data exactly once.
    const uint32_t deletions_before = privacy_guard_local_deletion_count();
    revoked = false;
    assert(privacy_guard_sync_consent(true, false, true, true, &revoked) ==
           PRIVACY_GUARD_OK);
    assert(revoked);
    assert(clear_calls == 1);
    assert(last_action == PRIVACY_GUARD_ACTION_GUARDIAN_REQUEST);
    assert(privacy_guard_local_deletion_count() == deletions_before + 1);
    assert(!privacy_guard_has_consent(PRIVACY_GUARD_DATA_IMAGE_UPLOAD));
    assert(privacy_guard_has_consent(PRIVACY_GUARD_DATA_AUDIO_UPLOAD));

    // An unchanged snapshot is neither destructive nor an error.
    clear_calls = 0;
    revoked = true;
    assert(privacy_guard_sync_consent(true, false, true, true, &revoked) ==
           PRIVACY_GUARD_OK);
    assert(!revoked);
    assert(clear_calls == 0);
}

}  // namespace

int main() {
    test_unregistered_handler_fails_instead_of_faking_success();
    test_registered_handler_runs_the_real_deletion();
    test_apply_consent_updates_every_class();
    test_sync_consent_erases_only_on_revocation();
    return 0;
}
