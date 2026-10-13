#include "cellular_sync_core.h"

#include <cassert>
#include <cstring>

int main() {
    // A well-formed platform policy maps onto the firmware contract.
    cellular_policy_t policy = {};
    assert(cellular_sync_core_parse_policy(
        1u,
        true,
        true,
        true,
        true,
        "cellular_first",
        5000u,
        20u,
        120u,
        7u,
        &policy
    ));
    assert(policy.schema_version == 1u);
    assert(policy.enabled == 1u);
    assert(policy.roaming_allowed == 1u);
    assert(policy.remote_wake_enabled == 1u);
    assert(policy.metered_upload_limit == 1u);
    assert(policy.preference == CELLULAR_PREFERENCE_CELLULAR_FIRST);
    assert(policy.monthly_quota_mb == 5000u);
    assert(policy.low_quota_warn_percent == 20u);
    assert(policy.keepalive_seconds == 120u);
    assert(policy.revision == 7u);

    // Out-of-range values are clamped instead of rejected.
    cellular_policy_t clamped = {};
    assert(cellular_sync_core_parse_policy(
        1u,
        true,
        false,
        false,
        false,
        "cellular_only",
        900000u,
        1u,
        1u,
        2u,
        &clamped
    ));
    assert(clamped.monthly_quota_mb <= CELLULAR_MAX_QUOTA_MB);
    assert(clamped.low_quota_warn_percent >= CELLULAR_MIN_WARN_PERCENT);
    assert(clamped.keepalive_seconds >= CELLULAR_MIN_KEEPALIVE_SECONDS);
    assert(clamped.revision == 2u);

    // A wrong schema version or preference is rejected.
    cellular_policy_t rejected = {};
    assert(!cellular_sync_core_parse_policy(
        2u, true, true, true, true, "wifi_first", 5000u, 20u, 120u, 1u,
        &rejected
    ));
    assert(!cellular_sync_core_parse_policy(
        1u, true, true, true, true, "satellite", 5000u, 20u, 120u, 1u,
        &rejected
    ));
    assert(!cellular_sync_core_parse_policy(
        1u, true, true, true, true, nullptr, 5000u, 20u, 120u, 1u, &rejected
    ));
    assert(!cellular_sync_core_parse_policy(
        1u, true, true, true, true, "wifi_first", 5000u, 20u, 120u, 0u,
        nullptr
    ));

    // Preference names round-trip into the platform vocabulary.
    assert(std::strcmp(
        cellular_sync_preference_name(CELLULAR_PREFERENCE_WIFI_FIRST),
        "wifi_first"
    ) == 0);
    assert(std::strcmp(
        cellular_sync_preference_name(CELLULAR_PREFERENCE_CELLULAR_FIRST),
        "cellular_first"
    ) == 0);
    assert(std::strcmp(
        cellular_sync_preference_name(CELLULAR_PREFERENCE_CELLULAR_ONLY),
        "cellular_only"
    ) == 0);

    // eSIM state names match the platform vocabulary.
    assert(std::strcmp(
        cellular_sync_esim_state_name(CELLULAR_ESIM_ABSENT),
        "absent"
    ) == 0);
    assert(std::strcmp(
        cellular_sync_esim_state_name(CELLULAR_ESIM_ACTIVE),
        "active"
    ) == 0);
    assert(std::strcmp(
        cellular_sync_esim_state_name(CELLULAR_ESIM_DISABLED),
        "disabled"
    ) == 0);

    return 0;
}
