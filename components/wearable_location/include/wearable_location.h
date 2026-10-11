#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stable wearable location schema version shared with the platform.
#define WEARABLE_LOCATION_SCHEMA_VERSION 1u

// Bounds for the upload interval in seconds and the retention window in days.
#define LOCATION_MIN_UPLOAD_INTERVAL_SECONDS 30u
#define LOCATION_MAX_UPLOAD_INTERVAL_SECONDS 86400u
#define LOCATION_MIN_RETENTION_DAYS 1u
#define LOCATION_MAX_RETENTION_DAYS 90u

// Bounds for a geofence radius in meters.
#define GEOFENCE_MIN_RADIUS_METERS 50u
#define GEOFENCE_MAX_RADIUS_METERS 10000u

// Location precision bands shared with the platform. A coarser band is a
// stricter privacy choice, so the default is the coarsest one.
typedef enum {
    LOCATION_PRECISION_COARSE = 0,
    LOCATION_PRECISION_BALANCED,
    LOCATION_PRECISION_PRECISE,
} location_precision_t;

// Geofence evaluation result shared with the platform.
typedef enum {
    GEOFENCE_INSIDE = 0,
    GEOFENCE_OUTSIDE,
} geofence_state_t;

// SOS lifecycle state shared with the platform. An active SOS is latched so a
// momentary button bounce cannot silently clear an emergency.
typedef enum {
    SOS_STATE_IDLE = 0,
    SOS_STATE_ARMED,
    SOS_STATE_TRIGGERED,
    SOS_STATE_CANCELLED,
} sos_state_t;

// One bounded wearable location policy.
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t permission_granted;
    uint8_t precision;
    uint8_t reserved;
    uint32_t upload_interval_seconds;
    uint16_t retention_days;
    uint16_t geofence_radius_meters;
} location_policy_t;

// Stable error codes for diagnostics and guardian-facing copy.
typedef enum {
    LOCATION_OK = 0,
    LOCATION_ERR_NULL_ARGUMENT,
    LOCATION_ERR_INVALID_POLICY,
    LOCATION_ERR_PERMISSION_DENIED,
    LOCATION_ERR_INVALID_STATE,
    LOCATION_ERR_STORAGE,
} location_error_t;

// Return the conservative default policy: disabled, no permission, coarse
// precision and the longest retention-safe interval.
location_policy_t location_default(void);

// Clamp one policy into the documented bounds.
location_policy_t location_clamp(const location_policy_t *policy);

// Return true when a policy is version-compatible and bounded.
bool location_is_valid(const location_policy_t *policy);

// Return the stable string for one location error code.
const char *location_error_name(location_error_t error);

// Return the stable string for one precision band.
const char *location_precision_name(location_precision_t precision);

// Return the stable string for one geofence state.
const char *geofence_state_name(geofence_state_t state);

// Return the stable string for one SOS state.
const char *sos_state_name(sos_state_t state);

// Return true when the policy may sample the sensor. A disabled policy or a
// missing permission stops sampling and upload entirely.
bool location_can_sample(const location_policy_t *policy);

// Return true when the upload window has elapsed. The comparison is inclusive
// so an interval exactly at the bound uploads in host tests and on the device
// alike.
bool location_should_upload(const location_policy_t *policy, uint32_t elapsed_ms);

// Return the geofence state for a measured distance and a bounded radius. The
// comparison is exclusive at the boundary so a point exactly on the radius
// stays inside.
geofence_state_t geofence_evaluate(uint32_t radius_meters, uint32_t distance_meters);

// Initialize the wearable location store.
esp_err_t wearable_init(void);

// Return true when the wearable location store is ready.
bool wearable_is_ready(void);

// Store a guardian policy, clamping out-of-range values.
esp_err_t wearable_set(const location_policy_t *policy);

// Return the currently applied policy.
location_policy_t wearable_get(void);

// Apply a policy that is already version-compatible and bounded.
esp_err_t wearable_apply(const location_policy_t *policy);

// Revoke the location permission and stop sampling. Revoking while sampling is
// active stops the upload path immediately so a revoked permission never keeps
// reporting a position.
esp_err_t wearable_revoke_permission(void);

// Arm the SOS button. Arming requires the location permission so an emergency
// can always carry a position.
esp_err_t wearable_sos_arm(void);

// Raise an SOS. A triggered SOS is latched until it is cancelled.
esp_err_t wearable_sos_trigger(void);

// Cancel an active SOS.
esp_err_t wearable_sos_cancel(void);

// Return the current SOS state.
sos_state_t wearable_sos_state(void);

// Return true when an active SOS is latched and must sync to the platform.
bool wearable_sos_active(void);

// Return the removable-module descriptor for wearable_location.
const module_descriptor_t *wearable_location_module_descriptor(void);

#ifdef __cplusplus
}
#endif
