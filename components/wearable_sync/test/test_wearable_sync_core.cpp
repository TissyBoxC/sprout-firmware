#include "wearable_sync_core.h"

#include <cassert>
#include <cstring>

int main() {
    // A well-formed platform policy maps onto the firmware contract and clamps
    // every field into the documented bounds.
    location_policy_t policy = {};
    assert(wearable_sync_core_parse_policy(
        1u,
        true,
        true,
        "balanced",
        600u,
        30u,
        500u,
        &policy
    ));
    assert(policy.schema_version == 1u);
    assert(policy.enabled == 1u);
    assert(policy.permission_granted == 1u);
    assert(policy.precision == LOCATION_PRECISION_BALANCED);
    assert(policy.upload_interval_seconds == 600u);
    assert(policy.retention_days == 30u);
    assert(policy.geofence_radius_meters == 500u);

    // Out-of-range values are clamped instead of rejected.
    location_policy_t clamped = {};
    assert(wearable_sync_core_parse_policy(
        1u,
        true,
        true,
        "precise",
        1u,
        1000u,
        1u,
        &clamped
    ));
    assert(clamped.upload_interval_seconds >= LOCATION_MIN_UPLOAD_INTERVAL_SECONDS);
    assert(clamped.retention_days <= LOCATION_MAX_RETENTION_DAYS);
    assert(clamped.geofence_radius_meters >= GEOFENCE_MIN_RADIUS_METERS);

    // A malformed schema version or precision is rejected outright.
    location_policy_t rejected = {};
    assert(!wearable_sync_core_parse_policy(
        2u, true, true, "balanced", 600u, 30u, 500u, &rejected
    ));
    assert(!wearable_sync_core_parse_policy(
        1u, true, true, "exact", 600u, 30u, 500u, &rejected
    ));
    assert(!wearable_sync_core_parse_policy(
        1u, true, true, nullptr, 600u, 30u, 500u, &rejected
    ));
    assert(!wearable_sync_core_parse_policy(
        1u, true, true, "balanced", 600u, 30u, 500u, nullptr
    ));

    // A disabled policy keeps sampling off.
    location_policy_t disabled = {};
    assert(wearable_sync_core_parse_policy(
        1u, false, false, "coarse", 600u, 30u, 500u, &disabled
    ));
    assert(!location_can_sample(&disabled));

    // A bounded position is accepted.
    wearable_sync_position_t valid = {1.3521, 103.8198, 12.5};
    assert(wearable_sync_core_position_is_valid(&valid));

    // A null, out-of-range or no-fix position is rejected.
    assert(!wearable_sync_core_position_is_valid(nullptr));
    wearable_sync_position_t bad_lat = {91.0, 103.8198, 12.5};
    assert(!wearable_sync_core_position_is_valid(&bad_lat));
    wearable_sync_position_t bad_lon = {1.3521, -181.0, 12.5};
    assert(!wearable_sync_core_position_is_valid(&bad_lon));
    wearable_sync_position_t bad_accuracy = {1.3521, 103.8198, -1.0};
    assert(!wearable_sync_core_position_is_valid(&bad_accuracy));
    wearable_sync_position_t no_fix = {0.0, 0.0, 0.0};
    assert(!wearable_sync_core_position_is_valid(&no_fix));

    // Precision names round-trip into the platform vocabulary.
    assert(std::strcmp(
        wearable_sync_precision_name(LOCATION_PRECISION_COARSE),
        "coarse"
    ) == 0);
    assert(std::strcmp(
        wearable_sync_precision_name(LOCATION_PRECISION_BALANCED),
        "balanced"
    ) == 0);
    assert(std::strcmp(
        wearable_sync_precision_name(LOCATION_PRECISION_PRECISE),
        "precise"
    ) == 0);

    return 0;
}
