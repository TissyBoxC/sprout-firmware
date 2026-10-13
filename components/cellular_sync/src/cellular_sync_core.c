#include "cellular_sync_core.h"

#include <string.h>

bool cellular_sync_core_preference_from_name(
    const char *name,
    cellular_preference_t *preference_out
) {
    if (name == NULL || preference_out == NULL) {
        return false;
    }
    if (strcmp(name, "wifi_first") == 0) {
        *preference_out = CELLULAR_PREFERENCE_WIFI_FIRST;
        return true;
    }
    if (strcmp(name, "cellular_first") == 0) {
        *preference_out = CELLULAR_PREFERENCE_CELLULAR_FIRST;
        return true;
    }
    if (strcmp(name, "cellular_only") == 0) {
        *preference_out = CELLULAR_PREFERENCE_CELLULAR_ONLY;
        return true;
    }
    return false;
}

const char *cellular_sync_preference_name(cellular_preference_t preference) {
    switch (preference) {
        case CELLULAR_PREFERENCE_WIFI_FIRST:
            return "wifi_first";
        case CELLULAR_PREFERENCE_CELLULAR_FIRST:
            return "cellular_first";
        case CELLULAR_PREFERENCE_CELLULAR_ONLY:
            return "cellular_only";
        default:
            return "wifi_first";
    }
}

const char *cellular_sync_esim_state_name(cellular_esim_state_t state) {
    switch (state) {
        case CELLULAR_ESIM_ABSENT:
            return "absent";
        case CELLULAR_ESIM_INSTALLED:
            return "installed";
        case CELLULAR_ESIM_ACTIVE:
            return "active";
        case CELLULAR_ESIM_DISABLED:
            return "disabled";
        default:
            return "absent";
    }
}

bool cellular_sync_core_parse_policy(
    uint32_t schema_version,
    bool enabled,
    bool roaming_allowed,
    bool remote_wake_enabled,
    bool metered_upload_limit,
    const char *preference,
    uint32_t monthly_quota_mb,
    uint32_t low_quota_warn_percent,
    uint32_t keepalive_seconds,
    cellular_policy_t *policy_out
) {
    if (policy_out == NULL || schema_version != CELLULAR_SCHEMA_VERSION) {
        return false;
    }
    cellular_preference_t parsed = CELLULAR_PREFERENCE_WIFI_FIRST;
    if (!cellular_sync_core_preference_from_name(preference, &parsed)) {
        return false;
    }
    cellular_policy_t policy = cellular_default();
    policy.schema_version = schema_version;
    policy.enabled = enabled ? 1u : 0u;
    policy.roaming_allowed = roaming_allowed ? 1u : 0u;
    policy.remote_wake_enabled = remote_wake_enabled ? 1u : 0u;
    policy.metered_upload_limit = metered_upload_limit ? 1u : 0u;
    policy.preference = (uint16_t)parsed;
    policy.monthly_quota_mb = monthly_quota_mb;
    policy.low_quota_warn_percent = (uint8_t)low_quota_warn_percent;
    policy.keepalive_seconds = (uint16_t)keepalive_seconds;
    const cellular_policy_t clamped = cellular_clamp(&policy);
    if (!cellular_is_valid(&clamped)) {
        return false;
    }
    *policy_out = clamped;
    return true;
}
