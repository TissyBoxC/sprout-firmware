// Host test for the conversation-context privacy gate.
//
// It links the production conversation window together with the privacy guard
// so the consent wiring is exercised exactly as the device runs it: no turn
// may be retained until the guardian grants conversation-history consent.

#include "conversation_context.h"
#include "privacy_guard.h"

#include <cassert>

namespace {

void test_history_is_denied_before_the_guard_is_ready() {
    // A guard that has never initialized reports no consent, so appends must
    // fail closed rather than retaining child dialogue.
    assert(!privacy_guard_has_consent(
        PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
    ));
    assert(conversation_context_append("user", "hello") ==
           ESP_ERR_INVALID_STATE);
}

void test_history_denied_until_consent_is_granted() {
    assert(privacy_guard_init() == ESP_OK);
    // The default state is deny-all, so a fresh guard still stores nothing.
    assert(conversation_context_init() == ESP_OK);
    assert(conversation_context_append("user", "hello") ==
           ESP_ERR_INVALID_STATE);
    assert(conversation_context_get_snapshot(NULL) == ESP_ERR_INVALID_ARG);

    conversation_context_snapshot_t snapshot = {};
    assert(conversation_context_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.turn_count == 0);

    // With the guardian consent present the window accepts a turn again.
    assert(privacy_guard_grant_consent(
               PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
           ) == PRIVACY_GUARD_OK);
    assert(conversation_context_append("user", "hello") == ESP_OK);
    assert(conversation_context_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.turn_count == 1);

    // Withdrawing consent blocks every further write without clearing on its
    // own; the composition root owns the eraser wiring.
    assert(privacy_guard_revoke_consent(
               PRIVACY_GUARD_DATA_CONVERSATION_HISTORY
           ) == PRIVACY_GUARD_OK);
    assert(conversation_context_append("user", "again") ==
           ESP_ERR_INVALID_STATE);
}

}  // namespace

int main() {
    test_history_is_denied_before_the_guard_is_ready();
    test_history_denied_until_consent_is_granted();
    return 0;
}
