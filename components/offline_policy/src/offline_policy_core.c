#include "offline_policy_core.h"

#include <stddef.h>

static uint16_t offline_policy_clamp_u16(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    if (value < minimum) {
        return (uint16_t)minimum;
    }
    if (value > maximum) {
        return (uint16_t)maximum;
    }
    return (uint16_t)value;
}

int64_t offline_policy_clamp_cache_bytes(int64_t value) {
    if (value < (int64_t)OFFLINE_POLICY_MIN_CACHE_BYTES) {
        return (int64_t)OFFLINE_POLICY_MIN_CACHE_BYTES;
    }
    if (value > (int64_t)OFFLINE_POLICY_MAX_CACHE_BYTES) {
        return (int64_t)OFFLINE_POLICY_MAX_CACHE_BYTES;
    }
    return value;
}

offline_policy_t offline_policy_default(void) {
    const offline_policy_t policy = {
        .schema_version = OFFLINE_POLICY_SCHEMA_VERSION,
        .offline_enabled = true,
        .max_cached_packages = OFFLINE_POLICY_DEFAULT_CACHED_PACKAGES,
        .max_cache_bytes = (int64_t)OFFLINE_POLICY_DEFAULT_CACHE_BYTES,
        .cache_ttl_hours = OFFLINE_POLICY_DEFAULT_CACHE_TTL_HOURS,
        .download_over_wifi_only = true,
        .revision = 0u,
    };
    return policy;
}

offline_policy_t offline_policy_clamp(const offline_policy_t *policy) {
    offline_policy_t clamped = offline_policy_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.offline_enabled = policy->offline_enabled;
    clamped.max_cached_packages = offline_policy_clamp_u16(
        policy->max_cached_packages,
        OFFLINE_POLICY_MIN_CACHED_PACKAGES,
        OFFLINE_POLICY_MAX_CACHED_PACKAGES
    );
    clamped.max_cache_bytes = offline_policy_clamp_cache_bytes(
        policy->max_cache_bytes
    );
    clamped.cache_ttl_hours = offline_policy_clamp_u16(
        policy->cache_ttl_hours,
        OFFLINE_POLICY_MIN_CACHE_TTL_HOURS,
        OFFLINE_POLICY_MAX_CACHE_TTL_HOURS
    );
    clamped.download_over_wifi_only = policy->download_over_wifi_only;
    clamped.revision = policy->revision;
    clamped.schema_version = OFFLINE_POLICY_SCHEMA_VERSION;
    return clamped;
}

bool offline_policy_is_valid(const offline_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != OFFLINE_POLICY_SCHEMA_VERSION) {
        return false;
    }
    if (policy->max_cached_packages < OFFLINE_POLICY_MIN_CACHED_PACKAGES ||
        policy->max_cached_packages > OFFLINE_POLICY_MAX_CACHED_PACKAGES) {
        return false;
    }
    if (policy->max_cache_bytes < (int64_t)OFFLINE_POLICY_MIN_CACHE_BYTES ||
        policy->max_cache_bytes > (int64_t)OFFLINE_POLICY_MAX_CACHE_BYTES) {
        return false;
    }
    if (policy->cache_ttl_hours < OFFLINE_POLICY_MIN_CACHE_TTL_HOURS ||
        policy->cache_ttl_hours > OFFLINE_POLICY_MAX_CACHE_TTL_HOURS) {
        return false;
    }
    return true;
}

bool offline_policy_core_accepts_revision(
    uint32_t current_revision,
    uint32_t remote_revision
) {
    return remote_revision >= current_revision;
}
