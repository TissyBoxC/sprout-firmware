#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of allowed content categories cached in one policy. */
#define PARENT_POLICY_MAX_ALLOWED_CATEGORIES 16

/** @brief Maximum number of disabled periods cached in one policy. */
#define PARENT_POLICY_MAX_DISABLED_PERIODS 64

/** @brief Maximum cached policy timestamp length including the terminator. */
#define PARENT_POLICY_TIMESTAMP_SIZE 32

/** @brief Maximum string length for one content category including the terminator. */
#define PARENT_POLICY_CATEGORY_SIZE 32

/** @brief Stable reasons explaining the most recent policy refresh failure. */
typedef enum {
    PARENT_POLICY_REFRESH_REASON_NONE = 0,
    PARENT_POLICY_REFRESH_REASON_NOT_READY,
    PARENT_POLICY_REFRESH_REASON_NETWORK,
    PARENT_POLICY_REFRESH_REASON_UNAUTHORIZED,
    PARENT_POLICY_REFRESH_REASON_NOT_FOUND,
    PARENT_POLICY_REFRESH_REASON_RESPONSE,
    PARENT_POLICY_REFRESH_REASON_VERSION,
    PARENT_POLICY_REFRESH_REASON_STORAGE,
    PARENT_POLICY_REFRESH_REASON_VOLUME,
    PARENT_POLICY_REFRESH_REASON_INTERNAL,
} parent_policy_refresh_reason_t;

/** @brief One disabled local-time period from the guardian policy. */
typedef struct {
    char start_time[6];
    char end_time[6];
} parent_policy_disabled_period_t;

/** @brief Complete in-memory policy snapshot returned to callers. */
typedef struct {
    int64_t policy_version;
    uint32_t daily_limit_minutes;
    char allowed_categories[PARENT_POLICY_MAX_ALLOWED_CATEGORIES]
                           [PARENT_POLICY_CATEGORY_SIZE];
    size_t allowed_category_count;
    parent_policy_disabled_period_t
        disabled_periods[PARENT_POLICY_MAX_DISABLED_PERIODS];
    size_t disabled_period_count;
    uint8_t max_volume_percent;
    char updated_at[PARENT_POLICY_TIMESTAMP_SIZE];
} parent_policy_snapshot_t;

/** @brief Lightweight cache and synchronization status. */
typedef struct {
    bool has_policy;
    bool is_synchronized;
    parent_policy_refresh_reason_t last_refresh_reason;
    esp_err_t last_error;
    int64_t policy_version;
    int64_t attempted_at;
} parent_policy_status_t;

/**
 * @brief Initialize parent policy storage and the initial volume ceiling.
 *
 * The last valid policy is loaded from encrypted NVS. When no policy is
 * cached, the compiled volume maximum is restored. A malformed or
 * inconsistent cached policy is discarded so a corrupted value cannot weaken
 * the guardian limit.
 */
esp_err_t parent_policy_init(void);

/** @brief Return true when the module is ready to serve snapshots. */
bool parent_policy_is_ready(void);

/**
 * @brief Fetch the effective policy for the authenticated device session.
 *
 * Uses GET /api/v1/devices/{device_id}/runtime/parent-policy. A successful
 * response must contain the documented policy object, a version greater than
 * the cached version, and valid bounded fields. Unknown or stale responses
 * leave the cached policy and live volume limit unchanged.
 *
 * Returns ESP_ERR_NOT_FOUND when the platform reports that no policy exists;
 * that response clears the cache and restores the compiled volume maximum.
 * All failures are recorded in parent_policy_get_status().
 */
esp_err_t parent_policy_refresh(void);

/**
 * @brief Fetch a policy only when the refresh interval has elapsed.
 *
 * Intended for periodic callers such as the runtime reporter. Attempts are
 * rate-limited to CONFIG_PARENT_POLICY_REFRESH_INTERVAL_SECONDS. When the
 * interval has not elapsed and a valid policy is cached, the function returns
 * ESP_OK without a network request.
 */
esp_err_t parent_policy_refresh_if_due(void);

/**
 * @brief Copy the cached policy into a caller-owned snapshot.
 *
 * Returns ESP_ERR_NOT_FOUND when no valid policy is cached.
 */
esp_err_t parent_policy_get_snapshot(parent_policy_snapshot_t *snapshot_out);

/** @brief Return the current cache and refresh status. */
parent_policy_status_t parent_policy_get_status(void);

/**
 * @brief Clear the cached policy and restore the compiled volume maximum.
 *
 * Used by factory-reset and policy-deletion paths. The operation succeeds
 * even when no policy was cached.
 */
esp_err_t parent_policy_clear(void);

/**
 * @brief Release runtime resources owned by the module.
 *
 * The persisted cache remains available to the next initialization. This
 * callback is intended for module_registry shutdown and test teardown.
 */
void parent_policy_shutdown(void);

/** @brief Return a stable string for one refresh reason. */
const char *parent_policy_refresh_reason_name(
    parent_policy_refresh_reason_t reason
);

/** @brief Return the removable-module descriptor for parent_policy. */
const module_descriptor_t *parent_policy_module_descriptor(void);

#ifdef __cplusplus
}
#endif
