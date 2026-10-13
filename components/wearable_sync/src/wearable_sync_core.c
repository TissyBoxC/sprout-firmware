#include "wearable_sync_core.h"

#include <string.h>

#include "esp_err.h"

// The platform accepts exactly these two geofence states from a device.
static bool wearable_sync_core_precision_from_name(
    const char *name,
    location_precision_t *precision_out
) {
    if (name == NULL || precision_out == NULL) {
        return false;
    }
    if (strcmp(name, "coarse") == 0) {
        *precision_out = LOCATION_PRECISION_COARSE;
        return true;
    }
    if (strcmp(name, "balanced") == 0) {
        *precision_out = LOCATION_PRECISION_BALANCED;
        return true;
    }
    if (strcmp(name, "precise") == 0) {
        *precision_out = LOCATION_PRECISION_PRECISE;
        return true;
    }
    return false;
}

bool wearable_sync_core_parse_policy(
    uint32_t schema_version,
    bool enabled,
    bool permission_granted,
    const char *precision,
    uint32_t upload_interval_seconds,
    uint32_t retention_days,
    uint32_t geofence_radius_meters,
    location_policy_t *policy_out
) {
    if (policy_out == NULL || schema_version != WEARABLE_LOCATION_SCHEMA_VERSION) {
        return false;
    }
    location_precision_t parsed_precision = LOCATION_PRECISION_COARSE;
    if (!wearable_sync_core_precision_from_name(precision, &parsed_precision)) {
        return false;
    }
    location_policy_t policy = location_default();
    policy.schema_version = schema_version;
    policy.enabled = enabled ? 1u : 0u;
    policy.permission_granted = permission_granted ? 1u : 0u;
    policy.precision = (uint8_t)parsed_precision;
    policy.upload_interval_seconds = upload_interval_seconds;
    policy.retention_days = (uint16_t)retention_days;
    policy.geofence_radius_meters = (uint16_t)geofence_radius_meters;
    const location_policy_t clamped = location_clamp(&policy);
    if (!location_is_valid(&clamped)) {
        return false;
    }
    *policy_out = clamped;
    return true;
}

bool wearable_sync_core_position_is_valid(
    const wearable_sync_position_t *position
) {
    if (position == NULL) {
        return false;
    }
    if (position->latitude < -90.0 || position->latitude > 90.0) {
        return false;
    }
    if (position->longitude < -180.0 || position->longitude > 180.0) {
        return false;
    }
    if (position->accuracy_meters < 0.0 || position->accuracy_meters > 100000.0) {
        return false;
    }
    // A zero/zero fix is the common "no fix" sentinel and is never uploaded.
    if (position->latitude == 0.0 && position->longitude == 0.0) {
        return false;
    }
    return true;
}

const char *wearable_sync_precision_name(location_precision_t precision) {
    switch (precision) {
        case LOCATION_PRECISION_COARSE:
            return "coarse";
        case LOCATION_PRECISION_BALANCED:
            return "balanced";
        case LOCATION_PRECISION_PRECISE:
            return "precise";
        default:
            return "coarse";
    }
}
