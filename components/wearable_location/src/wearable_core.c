#include "wearable_location.h"

#include <stddef.h>

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

static uint32_t clamp_u32(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

location_policy_t location_default(void) {
    const location_policy_t policy = {
        .schema_version = WEARABLE_LOCATION_SCHEMA_VERSION,
        // The default is a privacy-first wearable: no permission and no
        // sampling until a guardian grants it explicitly.
        .enabled = 0u,
        .permission_granted = 0u,
        .precision = (uint8_t)LOCATION_PRECISION_COARSE,
        .reserved = 0u,
        .upload_interval_seconds = 900u,
        .retention_days = 7u,
        .geofence_radius_meters = 500u,
    };
    return policy;
}

location_policy_t location_clamp(const location_policy_t *policy) {
    location_policy_t clamped = location_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.schema_version = WEARABLE_LOCATION_SCHEMA_VERSION;
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.permission_granted =
        policy->permission_granted != 0u ? 1u : 0u;
    clamped.precision = clamp_u8(
        policy->precision,
        (uint32_t)LOCATION_PRECISION_COARSE,
        (uint32_t)LOCATION_PRECISION_PRECISE
    );
    clamped.reserved = 0u;
    clamped.upload_interval_seconds = clamp_u32(
        policy->upload_interval_seconds,
        LOCATION_MIN_UPLOAD_INTERVAL_SECONDS,
        LOCATION_MAX_UPLOAD_INTERVAL_SECONDS
    );
    clamped.retention_days = clamp_u16(
        policy->retention_days,
        LOCATION_MIN_RETENTION_DAYS,
        LOCATION_MAX_RETENTION_DAYS
    );
    clamped.geofence_radius_meters = clamp_u16(
        policy->geofence_radius_meters,
        GEOFENCE_MIN_RADIUS_METERS,
        GEOFENCE_MAX_RADIUS_METERS
    );
    return clamped;
}

bool location_is_valid(const location_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != WEARABLE_LOCATION_SCHEMA_VERSION) {
        return false;
    }
    if (policy->precision > (uint8_t)LOCATION_PRECISION_PRECISE) {
        return false;
    }
    // Widen the uint16 fields before comparing so the bound check stays a real
    // runtime comparison rather than a compile-time constant fold.
    const uint32_t retention_days = policy->retention_days;
    const uint32_t radius_meters = policy->geofence_radius_meters;
    if (policy->upload_interval_seconds <
            LOCATION_MIN_UPLOAD_INTERVAL_SECONDS ||
        policy->upload_interval_seconds > LOCATION_MAX_UPLOAD_INTERVAL_SECONDS) {
        return false;
    }
    if (retention_days < LOCATION_MIN_RETENTION_DAYS ||
        retention_days > LOCATION_MAX_RETENTION_DAYS) {
        return false;
    }
    if (radius_meters < GEOFENCE_MIN_RADIUS_METERS ||
        radius_meters > GEOFENCE_MAX_RADIUS_METERS) {
        return false;
    }
    return true;
}

bool location_can_sample(const location_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    const location_policy_t bounded = location_clamp(policy);
    if (bounded.enabled == 0u) {
        return false;
    }
    if (bounded.permission_granted == 0u) {
        return false;
    }
    return true;
}

bool location_should_upload(
    const location_policy_t *policy,
    uint32_t elapsed_ms
) {
    const location_policy_t bounded = location_clamp(policy);
    const uint32_t limit_ms =
        (uint32_t)bounded.upload_interval_seconds * 1000u;
    return elapsed_ms >= limit_ms;
}

geofence_state_t geofence_evaluate(
    uint32_t radius_meters,
    uint32_t distance_meters
) {
    uint32_t bounded = radius_meters;
    if (bounded < GEOFENCE_MIN_RADIUS_METERS) {
        bounded = GEOFENCE_MIN_RADIUS_METERS;
    }
    if (bounded > GEOFENCE_MAX_RADIUS_METERS) {
        bounded = GEOFENCE_MAX_RADIUS_METERS;
    }
    // A point exactly on the radius stays inside so a boundary reading never
    // flaps the geofence.
    return distance_meters <= bounded ? GEOFENCE_INSIDE : GEOFENCE_OUTSIDE;
}

const char *location_error_name(location_error_t error) {
    switch (error) {
        case LOCATION_OK:
            return "ok";
        case LOCATION_ERR_NULL_ARGUMENT:
            return "null_argument";
        case LOCATION_ERR_INVALID_POLICY:
            return "invalid_policy";
        case LOCATION_ERR_PERMISSION_DENIED:
            return "permission_denied";
        case LOCATION_ERR_INVALID_STATE:
            return "invalid_state";
        case LOCATION_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

const char *location_precision_name(location_precision_t precision) {
    switch (precision) {
        case LOCATION_PRECISION_BALANCED:
            return "balanced";
        case LOCATION_PRECISION_PRECISE:
            return "precise";
        case LOCATION_PRECISION_COARSE:
        default:
            return "coarse";
    }
}

const char *geofence_state_name(geofence_state_t state) {
    switch (state) {
        case GEOFENCE_OUTSIDE:
            return "outside";
        case GEOFENCE_INSIDE:
        default:
            return "inside";
    }
}

const char *sos_state_name(sos_state_t state) {
    switch (state) {
        case SOS_STATE_ARMED:
            return "armed";
        case SOS_STATE_TRIGGERED:
            return "triggered";
        case SOS_STATE_CANCELLED:
            return "cancelled";
        case SOS_STATE_IDLE:
        default:
            return "idle";
    }
}
