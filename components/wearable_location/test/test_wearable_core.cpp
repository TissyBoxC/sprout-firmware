#include "wearable_location.h"

#include <cassert>
#include <cstring>

int main() {
    const location_policy_t defaults = location_default();
    assert(location_is_valid(&defaults));
    assert(defaults.enabled == 0u);
    assert(defaults.permission_granted == 0u);
    assert(defaults.precision == (uint8_t)LOCATION_PRECISION_COARSE);
    assert(defaults.upload_interval_seconds == 900u);
    assert(defaults.retention_days == 7u);

    // The default has no permission, so it cannot sample or upload.
    assert(!location_can_sample(&defaults));
    assert(!location_can_sample(nullptr));

    // Clamp pulls an out-of-range policy back inside every bound.
    location_policy_t huge = defaults;
    huge.enabled = 2u;
    huge.permission_granted = 3u;
    huge.precision = 42u;
    huge.upload_interval_seconds = 100000u;
    huge.retention_days = 60000u;
    huge.geofence_radius_meters = 60000u;
    const location_policy_t clamped = location_clamp(&huge);
    assert(clamped.enabled == 1u);
    assert(clamped.permission_granted == 1u);
    assert(clamped.precision == (uint8_t)LOCATION_PRECISION_PRECISE);
    assert(clamped.upload_interval_seconds ==
           LOCATION_MAX_UPLOAD_INTERVAL_SECONDS);
    assert(clamped.retention_days == LOCATION_MAX_RETENTION_DAYS);
    assert(clamped.geofence_radius_meters == GEOFENCE_MAX_RADIUS_METERS);
    assert(location_is_valid(&clamped));

    location_policy_t tiny = defaults;
    tiny.upload_interval_seconds = 1u;
    tiny.retention_days = 0u;
    tiny.geofence_radius_meters = 1u;
    const location_policy_t floored = location_clamp(&tiny);
    assert(floored.upload_interval_seconds ==
           LOCATION_MIN_UPLOAD_INTERVAL_SECONDS);
    assert(floored.retention_days == LOCATION_MIN_RETENTION_DAYS);
    assert(floored.geofence_radius_meters == GEOFENCE_MIN_RADIUS_METERS);

    assert(location_clamp(nullptr).enabled == defaults.enabled);

    // A version mismatch, an out-of-range precision or interval is refused.
    assert(!location_is_valid(nullptr));
    location_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!location_is_valid(&wrong_version));
    location_policy_t wrong_precision = defaults;
    wrong_precision.precision = 42u;
    assert(!location_is_valid(&wrong_precision));
    location_policy_t wrong_interval = defaults;
    wrong_interval.upload_interval_seconds = 1u;
    assert(!location_is_valid(&wrong_interval));

    // Sampling needs both the switch and the permission.
    location_policy_t granted = defaults;
    granted.enabled = 1u;
    assert(!location_can_sample(&granted));
    granted.permission_granted = 1u;
    assert(location_can_sample(&granted));

    // The upload window is inclusive at the bound.
    const uint32_t interval_ms =
        (uint32_t)granted.upload_interval_seconds * 1000u;
    assert(!location_should_upload(&granted, interval_ms - 1u));
    assert(location_should_upload(&granted, interval_ms));

    // The geofence boundary stays inside and clamps a wild radius.
    assert(geofence_evaluate(500u, 499u) == GEOFENCE_INSIDE);
    assert(geofence_evaluate(500u, 500u) == GEOFENCE_INSIDE);
    assert(geofence_evaluate(500u, 501u) == GEOFENCE_OUTSIDE);
    assert(geofence_evaluate(1u, 100u) == GEOFENCE_OUTSIDE);
    assert(geofence_evaluate(60000u, 10000u) == GEOFENCE_INSIDE);

    assert(std::strcmp(
               location_precision_name(LOCATION_PRECISION_COARSE),
               "coarse") == 0);
    assert(std::strcmp(
               location_precision_name(LOCATION_PRECISION_PRECISE),
               "precise") == 0);
    assert(std::strcmp(geofence_state_name(GEOFENCE_OUTSIDE), "outside") == 0);
    assert(std::strcmp(sos_state_name(SOS_STATE_TRIGGERED), "triggered") == 0);
    assert(std::strcmp(
               location_error_name(LOCATION_ERR_PERMISSION_DENIED),
               "permission_denied") == 0);
    return 0;
}
