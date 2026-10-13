#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wearable_location.h"
#include "wearable_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the policy parsing and bounds
 * are covered by host tests. */

/**
 * @brief Parse the platform location policy into the firmware contract.
 *
 * The precision string maps coarse/balanced/precise onto the firmware enum.
 * Returns false for a malformed schema version or precision so a bad response
 * is rejected instead of silently disabling location.
 */
bool wearable_sync_core_parse_policy(
    uint32_t schema_version,
    bool enabled,
    bool permission_granted,
    const char *precision,
    uint32_t upload_interval_seconds,
    uint32_t retention_days,
    uint32_t geofence_radius_meters,
    location_policy_t *policy_out
);

/** @brief Return true when a bounded position sample can be uploaded. */
bool wearable_sync_core_position_is_valid(
    const wearable_sync_position_t *position
);

/** @brief Stable text for one precision band, mirrored by the platform. */
const char *wearable_sync_precision_name(location_precision_t precision);

#ifdef __cplusplus
}
#endif
