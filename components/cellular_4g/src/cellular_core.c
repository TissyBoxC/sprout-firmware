#include "cellular_core.h"

#include <stddef.h>
#include <string.h>

static uint8_t clamp_u8(uint32_t value, uint32_t minimum, uint32_t maximum) {
    if (value < minimum) {
        return (uint8_t)minimum;
    }
    if (value > maximum) {
        return (uint8_t)maximum;
    }
    return (uint8_t)value;
}

static uint16_t clamp_u16(
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

static bool preference_supported(uint16_t preference) {
    switch (preference) {
        case CELLULAR_PREFERENCE_WIFI_FIRST:
        case CELLULAR_PREFERENCE_CELLULAR_FIRST:
        case CELLULAR_PREFERENCE_CELLULAR_ONLY:
            return true;
        default:
            return false;
    }
}

cellular_policy_t cellular_default(void) {
    const cellular_policy_t policy = {
        .schema_version = CELLULAR_SCHEMA_VERSION,
        .enabled = 1u,
        .roaming_allowed = 0u,
        .remote_wake_enabled = 1u,
        .metered_upload_limit = 1u,
        .preference = CELLULAR_PREFERENCE_WIFI_FIRST,
        .monthly_quota_mb = 2048u,
        .low_quota_warn_percent = 20u,
        .keepalive_seconds = 300u,
    };
    return policy;
}

cellular_policy_t cellular_clamp(const cellular_policy_t *policy) {
    cellular_policy_t clamped = cellular_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.roaming_allowed = policy->roaming_allowed != 0u ? 1u : 0u;
    clamped.remote_wake_enabled =
        policy->remote_wake_enabled != 0u ? 1u : 0u;
    clamped.metered_upload_limit =
        policy->metered_upload_limit != 0u ? 1u : 0u;
    clamped.preference = preference_supported(policy->preference)
        ? (uint16_t)policy->preference
        : (uint16_t)CELLULAR_PREFERENCE_WIFI_FIRST;
    // The quota is unsigned, so 0 is a valid "unlimited" sentinel; only the
    // upper bound needs clamping.
    if (policy->monthly_quota_mb > CELLULAR_MAX_QUOTA_MB) {
        clamped.monthly_quota_mb = CELLULAR_MAX_QUOTA_MB;
    } else {
        clamped.monthly_quota_mb = policy->monthly_quota_mb;
    }
    clamped.low_quota_warn_percent = clamp_u8(
        policy->low_quota_warn_percent,
        CELLULAR_MIN_WARN_PERCENT,
        CELLULAR_MAX_WARN_PERCENT
    );
    clamped.keepalive_seconds = clamp_u16(
        policy->keepalive_seconds,
        CELLULAR_MIN_KEEPALIVE_SECONDS,
        CELLULAR_MAX_KEEPALIVE_SECONDS
    );
    clamped.schema_version = CELLULAR_SCHEMA_VERSION;
    return clamped;
}

bool cellular_is_valid(const cellular_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != CELLULAR_SCHEMA_VERSION) {
        return false;
    }
    if (!preference_supported(policy->preference)) {
        return false;
    }
    if (policy->monthly_quota_mb > CELLULAR_MAX_QUOTA_MB) {
        return false;
    }
    if (policy->low_quota_warn_percent < CELLULAR_MIN_WARN_PERCENT ||
        policy->low_quota_warn_percent > CELLULAR_MAX_WARN_PERCENT) {
        return false;
    }
    if (policy->keepalive_seconds < CELLULAR_MIN_KEEPALIVE_SECONDS ||
        policy->keepalive_seconds > CELLULAR_MAX_KEEPALIVE_SECONDS) {
        return false;
    }
    return true;
}

bool cellular_should_attach(
    const cellular_policy_t *policy,
    bool wifi_connected,
    bool roaming
) {
    if (policy == NULL) {
        return false;
    }
    const cellular_policy_t bounded = cellular_clamp(policy);
    if (bounded.enabled == 0u) {
        return false;
    }
    if (roaming && bounded.roaming_allowed == 0u) {
        return false;
    }
    switch (bounded.preference) {
        case CELLULAR_PREFERENCE_CELLULAR_ONLY:
            return true;
        case CELLULAR_PREFERENCE_CELLULAR_FIRST:
            // Attach on cellular even when Wi-Fi is up so a preferred cellular
            // bearer wins, unless it is the roaming case rejected above.
            return true;
        case CELLULAR_PREFERENCE_WIFI_FIRST:
        default:
            return !wifi_connected;
    }
}

bool cellular_quota_exhausted(
    const cellular_policy_t *policy,
    uint32_t used_mb
) {
    if (policy == NULL) {
        return false;
    }
    if (policy->monthly_quota_mb == 0u) {
        // A zero quota means unlimited.
        return false;
    }
    return used_mb >= policy->monthly_quota_mb;
}

bool cellular_quota_warning(
    const cellular_policy_t *policy,
    uint32_t used_mb
) {
    if (policy == NULL || policy->monthly_quota_mb == 0u) {
        return false;
    }
    const uint32_t warn_threshold =
        (policy->monthly_quota_mb * policy->low_quota_warn_percent) / 100u;
    return used_mb >= warn_threshold;
}

bool cellular_upload_allowed(
    const cellular_policy_t *policy,
    bool on_cellular,
    uint32_t used_mb
) {
    if (policy == NULL) {
        return false;
    }
    if (!on_cellular) {
        return true;
    }
    if (policy->metered_upload_limit == 0u) {
        return true;
    }
    return !cellular_quota_exhausted(policy, used_mb);
}

bool cellular_remote_wake_enabled(const cellular_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    return policy->enabled != 0u && policy->remote_wake_enabled != 0u;
}

uint32_t cellular_day_key(uint32_t epoch_seconds) {
    return epoch_seconds / 86400u;
}

void cellular_usage_init(cellular_usage_t *usage) {
    if (usage == NULL) {
        return;
    }
    memset(usage, 0, sizeof(*usage));
}

void cellular_usage_add_mb(
    cellular_usage_t *usage,
    uint32_t megabytes,
    uint32_t now_epoch_seconds
) {
    if (usage == NULL) {
        return;
    }
    const uint32_t day_key = cellular_day_key(now_epoch_seconds);
    if (usage->billing_day_key != day_key && usage->used_mb != 0u) {
        usage->used_mb = 0u;
    }
    usage->billing_day_key = day_key;
    usage->used_mb += megabytes;
}

bool cellular_usage_quota_exhausted(
    const cellular_usage_t *usage,
    const cellular_policy_t *policy,
    uint32_t now_epoch_seconds
) {
    if (usage == NULL) {
        return false;
    }
    if (usage->billing_day_key != cellular_day_key(now_epoch_seconds)) {
        return false;
    }
    return cellular_quota_exhausted(policy, usage->used_mb);
}
