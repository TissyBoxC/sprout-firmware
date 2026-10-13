#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable cellular policy schema version shared with the platform. */
#define CELLULAR_SCHEMA_VERSION 1u

/** @brief Bounds for the monthly data quota in megabytes (0 = unlimited). */
#define CELLULAR_MIN_QUOTA_MB 0u
#define CELLULAR_MAX_QUOTA_MB 512000u

/** @brief Bounds for the low-quota warning threshold in percent. */
#define CELLULAR_MIN_WARN_PERCENT 5u
#define CELLULAR_MAX_WARN_PERCENT 100u

/** @brief Bounds for the keepalive interval in seconds. */
#define CELLULAR_MIN_KEEPALIVE_SECONDS 30u
#define CELLULAR_MAX_KEEPALIVE_SECONDS 3600u

/** @brief The preferred radio the device should attach to when available. */
typedef enum {
    CELLULAR_PREFERENCE_WIFI_FIRST = 0,
    CELLULAR_PREFERENCE_CELLULAR_FIRST,
    CELLULAR_PREFERENCE_CELLULAR_ONLY,
} cellular_preference_t;

/** @brief eSIM profile lifecycle states. */
typedef enum {
    CELLULAR_ESIM_ABSENT = 0,
    CELLULAR_ESIM_INSTALLED,
    CELLULAR_ESIM_ACTIVE,
    CELLULAR_ESIM_DISABLED,
} cellular_esim_state_t;

/** @brief One validated cellular policy applied by the connectivity layer. */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t roaming_allowed;
    uint8_t remote_wake_enabled;
    uint8_t metered_upload_limit;
    uint16_t preference;
    uint32_t monthly_quota_mb;
    uint8_t low_quota_warn_percent;
    uint16_t keepalive_seconds;
    uint32_t revision;
} cellular_policy_t;

/** @brief Live eSIM and modem state reported to the platform. */
typedef struct {
    uint32_t iccid_digits;
    uint8_t signal_percent;
    uint8_t registered;
    uint8_t esim_state;
} cellular_status_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    CELLULAR_OK = 0,
    CELLULAR_ERR_NULL_ARGUMENT,
    CELLULAR_ERR_INVALID_POLICY,
    CELLULAR_ERR_STORAGE,
} cellular_error_t;

/** @brief Return the conservative default policy. */
cellular_policy_t cellular_default(void);

/** @brief Clamp one policy into the documented bounds. */
cellular_policy_t cellular_clamp(const cellular_policy_t *policy);

/** @brief Return true when a policy is version-compatible and bounded. */
bool cellular_is_valid(const cellular_policy_t *policy);

/** @brief Return the stable string for one cellular error code. */
const char *cellular_error_name(cellular_error_t error);

/** @brief Apply the cached policy to the modem. */
esp_err_t cellular_apply(const cellular_policy_t *policy);

/** @brief Initialize the cellular policy store. */
esp_err_t cellular_init(void);

/** @brief Return true when the cellular policy store is ready. */
bool cellular_is_ready(void);

/** @brief Store a guardian policy, clamping out-of-range values. */
esp_err_t cellular_set(const cellular_policy_t *policy);

/** @brief Return the currently applied policy. */
cellular_policy_t cellular_get(void);

/**
 * @brief Decide whether the device should be attached to cellular right now.
 *
 * The pure decision function encodes the Wi-Fi/cellular preference and the
 * roaming and metered-upload policy so the same logic runs in host tests and on
 * the device.
 */
bool cellular_should_attach(
    const cellular_policy_t *policy,
    bool wifi_connected,
    bool roaming
);

/** @brief Return true when the monthly quota is exhausted. */
bool cellular_quota_exhausted(
    const cellular_policy_t *policy,
    uint32_t used_mb
);

/** @brief Return true when the used quota crossed the warning threshold. */
bool cellular_quota_warning(
    const cellular_policy_t *policy,
    uint32_t used_mb
);

/**
 * @brief Return true when a metered background upload may proceed.
 *
 * Over a metered cellular bearer the policy can block non-essential uploads
 * once the quota is reached, without affecting voice or emergency traffic.
 */
bool cellular_upload_allowed(
    const cellular_policy_t *policy,
    bool on_cellular,
    uint32_t used_mb
);

/** @brief Return true when the remote-wake capability is enabled. */
bool cellular_remote_wake_enabled(const cellular_policy_t *policy);

/** @brief Return the removable-module descriptor for cellular_4g. */
const module_descriptor_t *cellular_module_descriptor(void);

#ifdef __cplusplus
}
#endif
