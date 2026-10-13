#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable policy schema version shared with the platform contract. */
#define OFFLINE_POLICY_SCHEMA_VERSION 1u

/** @brief Bounds for the cached package count. */
#define OFFLINE_POLICY_MIN_CACHED_PACKAGES 1u
#define OFFLINE_POLICY_MAX_CACHED_PACKAGES 200u

/** @brief Cache byte bounds: 16 MiB minimum, 8 GiB maximum. */
#define OFFLINE_POLICY_MIN_CACHE_BYTES (16ull * 1024ull * 1024ull)
#define OFFLINE_POLICY_MAX_CACHE_BYTES (8ull * 1024ull * 1024ull * 1024ull)

/** @brief Cache TTL bounds in hours: one hour to one year. */
#define OFFLINE_POLICY_MIN_CACHE_TTL_HOURS 1u
#define OFFLINE_POLICY_MAX_CACHE_TTL_HOURS 8760u

/** @brief Conservative fallback policy served before an operator publishes. */
#define OFFLINE_POLICY_DEFAULT_CACHED_PACKAGES 20u
#define OFFLINE_POLICY_DEFAULT_CACHE_BYTES (512ull * 1024ull * 1024ull)
#define OFFLINE_POLICY_DEFAULT_CACHE_TTL_HOURS 720u

/** @brief One validated fleet offline-download policy applied by the device. */
typedef struct {
    uint32_t schema_version;
    bool offline_enabled;
    uint16_t max_cached_packages;
    int64_t max_cache_bytes;
    uint16_t cache_ttl_hours;
    bool download_over_wifi_only;
    uint32_t revision;
} offline_policy_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    OFFLINE_POLICY_OK = 0,
    OFFLINE_POLICY_ERR_NULL_ARGUMENT,
    OFFLINE_POLICY_ERR_INVALID_POLICY,
    OFFLINE_POLICY_ERR_STORAGE,
} offline_policy_error_t;

/** @brief Return the conservative default policy. */
offline_policy_t offline_policy_default(void);

/** @brief Clamp one policy into the documented bounds. */
offline_policy_t offline_policy_clamp(const offline_policy_t *policy);

/** @brief Return true when a policy is version-compatible and bounded. */
bool offline_policy_is_valid(const offline_policy_t *policy);

/** @brief Return the stable string for one offline policy error code. */
const char *offline_policy_error_name(offline_policy_error_t error);

/** @brief Initialize the offline policy store from the configuration store. */
esp_err_t offline_policy_init(void);

/** @brief Return true when the offline policy store is ready. */
bool offline_policy_is_ready(void);

/**
 * @brief Store a fetched fleet policy, clamping out-of-range values.
 *
 * A policy whose revision is older than the applied one is ignored so an out
 * of order delivery cannot roll the cache budget back. An equal revision
 * refreshes the stored policy.
 */
esp_err_t offline_policy_set(const offline_policy_t *policy);

/** @brief Return the currently applied policy. */
offline_policy_t offline_policy_get(void);

/** @brief Return the removable-module descriptor for offline_policy. */
const module_descriptor_t *offline_policy_module_descriptor(void);

#ifdef __cplusplus
}
#endif
