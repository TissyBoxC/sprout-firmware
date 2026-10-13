#include "connectivity_policy_sync_core.h"

#include <string.h>

connectivity_policy_state_t connectivity_policy_state_default(void) {
    const connectivity_policy_state_t state = {
        .schema_version = CONNECTIVITY_POLICY_SCHEMA_VERSION,
        .enabled = 1u,
        .roaming_allowed = 0u,
        .remote_wake_enabled = 1u,
        .metered_upload_limit = 1u,
        .preference = CONNECTIVITY_POLICY_PREFERENCE_WIFI_FIRST,
        .revision = 0u,
    };
    return state;
}

bool connectivity_policy_preference_from_name(
    const char *name,
    connectivity_policy_preference_t *preference
) {
    if (name == NULL || preference == NULL) {
        return false;
    }
    if (strcmp(name, "wifi_first") == 0) {
        *preference = CONNECTIVITY_POLICY_PREFERENCE_WIFI_FIRST;
        return true;
    }
    if (strcmp(name, "cellular_first") == 0) {
        *preference = CONNECTIVITY_POLICY_PREFERENCE_CELLULAR_FIRST;
        return true;
    }
    if (strcmp(name, "cellular_only") == 0) {
        *preference = CONNECTIVITY_POLICY_PREFERENCE_CELLULAR_ONLY;
        return true;
    }
    return false;
}

connectivity_policy_state_t connectivity_policy_sync_core_build(
    bool enabled,
    bool roaming_allowed,
    bool remote_wake_enabled,
    bool metered_upload_limit,
    const char *preference,
    int revision
) {
    connectivity_policy_state_t state = connectivity_policy_state_default();
    state.enabled = enabled ? 1u : 0u;
    state.roaming_allowed = roaming_allowed ? 1u : 0u;
    state.remote_wake_enabled = remote_wake_enabled ? 1u : 0u;
    state.metered_upload_limit = metered_upload_limit ? 1u : 0u;
    connectivity_policy_preference_t parsed =
        CONNECTIVITY_POLICY_PREFERENCE_WIFI_FIRST;
    if (connectivity_policy_preference_from_name(preference, &parsed)) {
        state.preference = (uint16_t)parsed;
    }
    state.revision = revision < 0 ? 0u : (uint32_t)revision;
    state.schema_version = CONNECTIVITY_POLICY_SCHEMA_VERSION;
    return state;
}
