#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable video session schema version shared with the platform. */
#define VIDEO_SESSION_SCHEMA_VERSION 1u

/** @brief Bounds for the ring timeout in seconds. */
#define VIDEO_MIN_RING_TIMEOUT_SECONDS 5u
#define VIDEO_MAX_RING_TIMEOUT_SECONDS 120u

/** @brief Bounds for the maximum session length in seconds. */
#define VIDEO_MIN_MAX_SESSION_SECONDS 60u
#define VIDEO_MAX_MAX_SESSION_SECONDS 7200u

/** @brief Normalized call state shared with the platform. */
typedef enum {
    VIDEO_STATE_IDLE = 0,
    VIDEO_STATE_RINGING,
    VIDEO_STATE_ACTIVE,
    VIDEO_STATE_ENDED,
    VIDEO_STATE_REJECTED,
    VIDEO_STATE_FAILED,
} video_state_t;

/** @brief Bounded media quality band shared with the platform. */
typedef enum {
    VIDEO_QUALITY_UNKNOWN = 0,
    VIDEO_QUALITY_GOOD,
    VIDEO_QUALITY_FAIR,
    VIDEO_QUALITY_POOR,
} video_quality_t;

/** @brief Stable end reasons shared with the platform. */
typedef enum {
    VIDEO_END_NONE = 0,
    VIDEO_END_HANGUP,
    VIDEO_END_REJECTED,
    VIDEO_END_TIMEOUT,
    VIDEO_END_NETWORK,
    VIDEO_END_PERMISSION_REVOKED,
} video_end_reason_t;

/** @brief One bounded video session policy. */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t camera_allowed;
    uint8_t microphone_allowed;
    uint8_t require_child_consent;
    uint8_t quality_floor;
    uint8_t reserved;
    uint16_t ring_timeout_seconds;
    uint16_t max_session_seconds;
    uint32_t revision;
} video_policy_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    VIDEO_OK = 0,
    VIDEO_ERR_NULL_ARGUMENT,
    VIDEO_ERR_INVALID_POLICY,
    VIDEO_ERR_INVALID_STATE,
    VIDEO_ERR_PERMISSION_DENIED,
    VIDEO_ERR_STORAGE,
} video_error_t;

/**
 * @brief One guardian policy as carried by the device endpoint.
 *
 * The quality floor is a catalog name ("unknown", "good", "fair", "poor")
 * rather than the firmware enum so the network layer can populate it without
 * knowing the firmware catalog. The string is owned by the caller.
 */
typedef struct {
    bool enabled;
    bool camera_allowed;
    bool microphone_allowed;
    bool require_child_consent;
    const char *quality_floor;
    int ring_timeout_seconds;
    int max_session_seconds;
    int revision;
} video_remote_t;

/**
 * @brief One bounded session status snapshot for the device endpoint report.
 *
 * The catalog names are stable shared contract strings; the snapshot is
 * read-only state that the sync worker serialises into the status report.
 */
typedef struct {
    bool supported;
    bool camera_allowed;
    bool microphone_allowed;
    video_state_t call_state;
    video_quality_t quality;
    video_end_reason_t last_end_reason;
    uint32_t revision;
} video_session_status_t;

/** @brief Return the conservative default policy (camera and mic off). */
video_policy_t video_default(void);

/** @brief Clamp one policy into the documented bounds. */
video_policy_t video_clamp(const video_policy_t *policy);

/** @brief Return true when a policy is version-compatible and bounded. */
bool video_is_valid(const video_policy_t *policy);

/** @brief Return the stable string for one video error code. */
const char *video_error_name(video_error_t error);

/** @brief Return the stable string for one call state. */
const char *video_state_name(video_state_t state);

/** @brief Return the stable string for one quality band. */
const char *video_quality_name(video_quality_t quality);

/** @brief Return the stable string for one end reason. */
const char *video_end_reason_name(video_end_reason_t reason);

/** @brief Initialize the video session store. */
esp_err_t video_init(void);

/** @brief Return true when the video session store is ready. */
bool video_is_ready(void);

/** @brief Store a guardian policy, clamping out-of-range values. */
esp_err_t video_set(const video_policy_t *policy);

/** @brief Return the currently applied policy. */
video_policy_t video_get(void);

/** @brief Apply a policy that is already version-compatible and bounded. */
esp_err_t video_apply(const video_policy_t *policy);

/**
 * @brief Apply a policy fetched from the device endpoint.
 *
 * Maps the catalog quality-floor name to the firmware enum and clamps every
 * field. Returns ESP_ERR_INVALID_ARG when the quality floor is not in its
 * catalog, so a malformed response is rejected instead of silently defaulting.
 */
esp_err_t video_apply_remote(const video_remote_t *remote);

// Begin an incoming call. A call only rings when the policy is enabled and at
// least the microphone is permitted. When the policy requires child consent the
// call stays ringing until `video_accept` runs, so a device never starts
// capturing silently.
esp_err_t video_ring(void);

/** @brief Accept a ringing call and enter the active state. */
esp_err_t video_accept(void);

/** @brief End the current call with a bounded reason. */
esp_err_t video_end(video_end_reason_t reason);

/** @brief Report a bounded media quality band. */
esp_err_t video_report_quality(video_quality_t quality);

// Revoke one media permission and end the call if it was in use. Revoking the
// camera or the microphone while the sensor is active ends the call
// immediately with the permission-revoked reason, so a revoked permission never
// leaves a silent capture running.
esp_err_t video_revoke_permission(bool camera, bool microphone);

/** @brief Return the current call state. */
video_state_t video_state(void);

/** @brief Return the reason the last call ended with. */
video_end_reason_t video_last_end_reason(void);

/** @brief Return the current bounded quality band. */
video_quality_t video_quality(void);

/** @brief Return one bounded session status snapshot for the status report. */
video_session_status_t video_session_status(void);

/** @brief Return true when the sensor may capture under the current policy. */
bool video_can_capture(const video_policy_t *policy);

// Return true when the ring window has elapsed. The comparison is inclusive so
// a ring time exactly at the bound times out in host tests and on the device
// alike.
bool video_ring_timed_out(const video_policy_t *policy, uint32_t elapsed_ms);

/** @brief Return the removable-module descriptor for video_session. */
const module_descriptor_t *video_session_module_descriptor(void);

#ifdef __cplusplus
}
#endif
