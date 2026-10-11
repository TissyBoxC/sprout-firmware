#include "family_linkage.h"

#include <cassert>
#include <cstring>

int main() {
    const family_policy_t defaults = family_default();
    assert(family_is_valid(&defaults));
    assert(defaults.enabled == 1u);
    assert(defaults.group_members == 1u);
    assert(defaults.conflict_strategy == (uint8_t)CONFLICT_REMOTE_WINS);

    // A family of one still routes to a device and the family, but not to a
    // group because there is no second member.
    assert(family_route_allowed(&defaults, ROUTE_SCOPE_DEVICE));
    assert(family_route_allowed(&defaults, ROUTE_SCOPE_FAMILY));
    assert(!family_route_allowed(&defaults, ROUTE_SCOPE_GROUP));
    assert(!family_route_allowed(nullptr, ROUTE_SCOPE_FAMILY));

    // Clamp pulls an out-of-range policy back inside every bound.
    family_policy_t huge = defaults;
    huge.enabled = 2u;
    huge.group_members = 200u;
    huge.conflict_strategy = 99u;
    huge.sync_retry_seconds = 60000u;
    huge.sync_max_attempts = 200u;
    const family_policy_t clamped = family_clamp(&huge);
    assert(clamped.enabled == 1u);
    assert(clamped.group_members == FAMILY_MAX_GROUP_MEMBERS);
    assert(clamped.conflict_strategy == (uint8_t)CONFLICT_DEFER);
    assert(clamped.sync_retry_seconds == FAMILY_MAX_SYNC_RETRY_SECONDS);
    assert(clamped.sync_max_attempts == FAMILY_MAX_SYNC_ATTEMPTS);
    assert(family_is_valid(&clamped));

    family_policy_t tiny = defaults;
    tiny.group_members = 0u;
    tiny.sync_retry_seconds = 1u;
    tiny.sync_max_attempts = 0u;
    const family_policy_t floored = family_clamp(&tiny);
    assert(floored.group_members == FAMILY_MIN_GROUP_MEMBERS);
    assert(floored.sync_retry_seconds == FAMILY_MIN_SYNC_RETRY_SECONDS);
    assert(floored.sync_max_attempts == 1u);

    assert(family_clamp(nullptr).group_members == defaults.group_members);

    // A version mismatch or an out-of-range field is refused.
    assert(!family_is_valid(nullptr));
    family_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!family_is_valid(&wrong_version));
    family_policy_t wrong_members = defaults;
    wrong_members.group_members = 0u;
    assert(!family_is_valid(&wrong_members));

    // A group route needs a second member.
    family_policy_t grouped = defaults;
    grouped.group_members = 3u;
    assert(family_route_allowed(&grouped, ROUTE_SCOPE_GROUP));
    family_policy_t disabled = defaults;
    disabled.enabled = 0u;
    assert(!family_route_allowed(&disabled, ROUTE_SCOPE_FAMILY));
    assert(family_route_allowed(&disabled, ROUTE_SCOPE_DEVICE));

    // A higher version always wins regardless of the strategy.
    sync_version_pair_t newer_local = {5u, 3u};
    assert(family_resolve_conflict(&defaults, newer_local) == CONFLICT_LOCAL_WINS);
    sync_version_pair_t newer_remote = {3u, 5u};
    assert(family_resolve_conflict(&defaults, newer_remote) == CONFLICT_REMOTE_WINS);

    // A same-version tie follows the configured strategy.
    sync_version_pair_t tie = {4u, 4u};
    family_policy_t defer = defaults;
    defer.conflict_strategy = (uint8_t)CONFLICT_DEFER;
    assert(family_resolve_conflict(&defer, tie) == CONFLICT_DEFER);

    // Equal versions are already consistent; a tie with defer is a conflict.
    assert(family_sync_state(&defaults, tie) == SYNC_STATE_APPLIED);
    assert(family_sync_state(&defer, tie) == SYNC_STATE_CONFLICT);
    assert(family_sync_state(&defaults, newer_remote) == SYNC_STATE_APPLIED);

    // The retry window is inclusive at the bound.
    const uint32_t retry_ms = (uint32_t)defaults.sync_retry_seconds * 1000u;
    assert(!family_sync_should_retry(&defaults, retry_ms - 1u));
    assert(family_sync_should_retry(&defaults, retry_ms));

    assert(std::strcmp(route_scope_name(ROUTE_SCOPE_GROUP), "group") == 0);
    assert(std::strcmp(
               conflict_strategy_name(CONFLICT_REMOTE_WINS),
               "remote_wins") == 0);
    assert(std::strcmp(sync_state_name(SYNC_STATE_CONFLICT), "conflict") == 0);
    assert(std::strcmp(
               family_error_name(FAMILY_ERR_INVALID_ROUTE),
               "invalid_route") == 0);
    return 0;
}
