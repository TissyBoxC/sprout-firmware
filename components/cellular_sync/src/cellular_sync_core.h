#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cellular_4g.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the policy mapping is covered
 * by host tests. */

/** @brief Map the platform bearer preference onto the firmware enum. */
bool cellular_sync_core_preference_from_name(
    const char *name,
    cellular_preference_t *preference_out
);

/** @brief Stable text for one firmware preference, mirrored by the platform. */
const char *cellular_sync_preference_name(cellular_preference_t preference);

/** @brief Map the firmware eSIM state onto the platform vocabulary. */
const char *cellular_sync_esim_state_name(cellular_esim_state_t state);

/**
 * @brief Build one applied policy from the platform response, clamping bounds.
 *
 * A wrong schema version is rejected so a newer contract is never applied
 * with missing fields.
 */
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
);

#ifdef __cplusplus
}
#endif
