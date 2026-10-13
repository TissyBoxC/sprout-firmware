#include "offline_policy_core.h"

#include <cassert>

int main() {
    // The default policy is valid and bounded.
    const offline_policy_t fallback = offline_policy_default();
    assert(fallback.schema_version == OFFLINE_POLICY_SCHEMA_VERSION);
    assert(fallback.offline_enabled);
    assert(fallback.download_over_wifi_only);
    assert(offline_policy_is_valid(&fallback));

    // Out-of-range fields are clamped into the approved bounds.
    offline_policy_t wide = fallback;
    wide.max_cached_packages = 0;
    wide.max_cache_bytes = -1;
    wide.cache_ttl_hours = 0;
    const offline_policy_t clamped_low = offline_policy_clamp(&wide);
    assert(clamped_low.max_cached_packages ==
           OFFLINE_POLICY_MIN_CACHED_PACKAGES);
    assert(clamped_low.max_cache_bytes ==
           (int64_t)OFFLINE_POLICY_MIN_CACHE_BYTES);
    assert(clamped_low.cache_ttl_hours == OFFLINE_POLICY_MIN_CACHE_TTL_HOURS);
    assert(offline_policy_is_valid(&clamped_low));

    offline_policy_t high = fallback;
    high.max_cached_packages = 60000;
    high.max_cache_bytes = (int64_t)OFFLINE_POLICY_MAX_CACHE_BYTES * 4;
    high.cache_ttl_hours = 60000;
    const offline_policy_t clamped_high = offline_policy_clamp(&high);
    assert(clamped_high.max_cached_packages ==
           OFFLINE_POLICY_MAX_CACHED_PACKAGES);
    assert(clamped_high.max_cache_bytes ==
           (int64_t)OFFLINE_POLICY_MAX_CACHE_BYTES);
    assert(clamped_high.cache_ttl_hours == OFFLINE_POLICY_MAX_CACHE_TTL_HOURS);
    assert(offline_policy_is_valid(&clamped_high));

    // A null pointer clamps to the conservative default.
    const offline_policy_t null_clamped = offline_policy_clamp(nullptr);
    assert(null_clamped.max_cached_packages ==
           OFFLINE_POLICY_DEFAULT_CACHED_PACKAGES);

    // A policy with a mismatched schema version is rejected as-is.
    offline_policy_t stale_schema = fallback;
    stale_schema.schema_version = 99u;
    assert(!offline_policy_is_valid(&stale_schema));
    assert(!offline_policy_is_valid(nullptr));

    // Only a revision at or above the applied one is accepted.
    assert(offline_policy_core_accepts_revision(4u, 5u));
    assert(offline_policy_core_accepts_revision(4u, 4u));
    assert(!offline_policy_core_accepts_revision(4u, 3u));

    return 0;
}
