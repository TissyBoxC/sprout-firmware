#include "cellular_4g.h"
#include "cellular_core.h"

#include <cassert>

int main() {
    const cellular_policy_t defaults = cellular_default();
    assert(cellular_is_valid(&defaults));
    assert(defaults.monthly_quota_mb == 2048u);
    assert(defaults.preference == CELLULAR_PREFERENCE_WIFI_FIRST);

    cellular_policy_t out_of_range = defaults;
    out_of_range.preference = 99u;
    out_of_range.low_quota_warn_percent = 0u;
    out_of_range.keepalive_seconds = 1u;
    out_of_range.monthly_quota_mb = 9999999u;
    const cellular_policy_t clamped = cellular_clamp(&out_of_range);
    assert(clamped.preference == CELLULAR_PREFERENCE_WIFI_FIRST);
    assert(clamped.low_quota_warn_percent == CELLULAR_MIN_WARN_PERCENT);
    assert(clamped.keepalive_seconds == CELLULAR_MIN_KEEPALIVE_SECONDS);
    assert(clamped.monthly_quota_mb == CELLULAR_MAX_QUOTA_MB);
    assert(cellular_is_valid(&clamped));

    assert(!cellular_is_valid(nullptr));
    cellular_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!cellular_is_valid(&wrong_version));
    assert(cellular_clamp(nullptr).monthly_quota_mb == defaults.monthly_quota_mb);

    // A Wi-Fi-first policy only attaches when Wi-Fi is down.
    assert(cellular_should_attach(&defaults, false, false));
    assert(!cellular_should_attach(&defaults, true, false));

    // A roaming attach is rejected unless roaming is allowed.
    assert(!cellular_should_attach(&defaults, false, true));
    cellular_policy_t roaming = defaults;
    roaming.roaming_allowed = 1u;
    assert(cellular_should_attach(&roaming, false, true));

    // A cellular-first policy attaches even when Wi-Fi is up.
    cellular_policy_t cellular_first = defaults;
    cellular_first.preference = CELLULAR_PREFERENCE_CELLULAR_FIRST;
    assert(cellular_should_attach(&cellular_first, true, false));

    // A disabled radio never attaches.
    cellular_policy_t disabled = defaults;
    disabled.enabled = 0u;
    assert(!cellular_should_attach(&disabled, false, false));
    assert(!cellular_remote_wake_enabled(&disabled));

    // Quota exhaustion is inclusive, and a zero quota is unlimited.
    assert(!cellular_quota_exhausted(&defaults, 2047u));
    assert(cellular_quota_exhausted(&defaults, 2048u));
    cellular_policy_t unlimited = defaults;
    unlimited.monthly_quota_mb = 0u;
    assert(!cellular_quota_exhausted(&unlimited, 100000u));

    // The warning threshold is the configured percentage of the quota.
    assert(!cellular_quota_warning(&defaults, 400u));
    assert(cellular_quota_warning(&defaults, 410u));

    // Metered uploads stop at the quota only when the limit is enabled.
    assert(cellular_upload_allowed(&defaults, false, 5000u));
    assert(!cellular_upload_allowed(&defaults, true, 2048u));
    cellular_policy_t unmetered = defaults;
    unmetered.metered_upload_limit = 0u;
    assert(cellular_upload_allowed(&unmetered, true, 2048u));

    // Usage rolls over at the UTC day boundary.
    cellular_usage_t usage;
    cellular_usage_init(&usage);
    const uint32_t base = 1700000000u;
    cellular_usage_add_mb(&usage, 100u, base);
    assert(usage.used_mb == 100u);
    cellular_usage_add_mb(&usage, 50u, base);
    assert(usage.used_mb == 150u);
    assert(!cellular_usage_quota_exhausted(&usage, &defaults, base));
    cellular_usage_add_mb(&usage, 2000u, base + 86400u);
    assert(usage.used_mb == 2000u);
    assert(!cellular_usage_quota_exhausted(
        &usage,
        &defaults,
        base + 86400u
    ));
    return 0;
}
