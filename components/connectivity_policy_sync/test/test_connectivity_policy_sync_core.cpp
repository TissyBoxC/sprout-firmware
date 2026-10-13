#include "connectivity_policy_sync_core.h"

#include <cassert>

int main() {
    const connectivity_policy_state_t defaults =
        connectivity_policy_state_default();
    assert(defaults.schema_version == CONNECTIVITY_POLICY_SCHEMA_VERSION);
    assert(defaults.enabled == 1u);
    assert(defaults.preference == CONNECTIVITY_POLICY_PREFERENCE_WIFI_FIRST);
    assert(defaults.revision == 0u);

    // A well-formed platform policy maps onto the state contract.
    const connectivity_policy_state_t policy =
        connectivity_policy_sync_core_build(
            true,
            true,
            false,
            false,
            "cellular_first",
            5
        );
    assert(policy.enabled == 1u);
    assert(policy.roaming_allowed == 1u);
    assert(policy.remote_wake_enabled == 0u);
    assert(policy.metered_upload_limit == 0u);
    assert(policy.preference == CONNECTIVITY_POLICY_PREFERENCE_CELLULAR_FIRST);
    assert(policy.revision == 5u);

    // An unknown preference falls back to Wi-Fi-first and a negative revision
    // is treated as zero.
    const connectivity_policy_state_t fallback =
        connectivity_policy_sync_core_build(
            false,
            false,
            false,
            false,
            "satellite",
            -3
        );
    assert(fallback.preference == CONNECTIVITY_POLICY_PREFERENCE_WIFI_FIRST);
    assert(fallback.revision == 0u);

    // The shared preference catalog round-trips through the parser.
    connectivity_policy_preference_t parsed =
        CONNECTIVITY_POLICY_PREFERENCE_WIFI_FIRST;
    assert(connectivity_policy_preference_from_name(
        "cellular_only",
        &parsed
    ));
    assert(parsed == CONNECTIVITY_POLICY_PREFERENCE_CELLULAR_ONLY);
    assert(!connectivity_policy_preference_from_name("hybrid", &parsed));
    assert(!connectivity_policy_preference_from_name(nullptr, &parsed));

    return 0;
}
