#include "family_linkage.h"

#include <cassert>

int main() {
    assert(family_init() == ESP_OK);
    assert(family_is_ready());
    assert(family_last_sync_state() == SYNC_STATE_IDLE);
    assert(!family_sync_exhausted());

    const family_policy_t defaults = family_get();
    assert(defaults.enabled == 1u);

    // A group route needs a second member; enable a group of three.
    family_policy_t grouped = defaults;
    grouped.group_members = 3u;
    grouped.conflict_strategy = (uint8_t)CONFLICT_DEFER;
    grouped.sync_max_attempts = 2u;
    assert(family_set(&grouped) == ESP_OK);
    const family_policy_t applied = family_get();
    assert(family_route_allowed(&applied, ROUTE_SCOPE_GROUP));

    // A same-version tie with defer records a conflict.
    sync_version_pair_t tie = {4u, 4u};
    assert(family_record_sync(tie) == SYNC_STATE_CONFLICT);
    assert(family_last_sync_state() == SYNC_STATE_CONFLICT);
    // One more conflict reaches the two-attempt budget.
    assert(family_record_sync(tie) == SYNC_STATE_CONFLICT);
    assert(family_sync_exhausted());

    // A newer remote version applies and resets the retry budget.
    sync_version_pair_t newer_remote = {4u, 7u};
    assert(family_record_sync(newer_remote) == SYNC_STATE_APPLIED);
    assert(family_last_sync_state() == SYNC_STATE_APPLIED);
    assert(!family_sync_exhausted());

    // With a deterministic strategy an equal version is already consistent.
    family_policy_t deterministic = family_get();
    deterministic.conflict_strategy = (uint8_t)CONFLICT_REMOTE_WINS;
    assert(family_set(&deterministic) == ESP_OK);
    sync_version_pair_t equal = {9u, 9u};
    assert(family_record_sync(equal) == SYNC_STATE_APPLIED);

    // Persistence round trip keeps the edited policy.
    family_policy_t persisted = family_get();
    persisted.group_members = 5u;
    persisted.sync_retry_seconds = 120u;
    assert(family_set(&persisted) == ESP_OK);
    assert(family_init() == ESP_OK);
    assert(family_get().group_members == 5u);
    assert(family_get().sync_retry_seconds == 120u);
    // A re-init clears the transient sync state.
    assert(family_last_sync_state() == SYNC_STATE_IDLE);

    // A policy that is not version-compatible is refused by apply.
    family_policy_t invalid;
    invalid.schema_version = 42u;
    assert(family_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(family_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = family_linkage_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
