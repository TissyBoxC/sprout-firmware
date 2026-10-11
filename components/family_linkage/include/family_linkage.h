#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stable family-linkage schema version shared with the platform.
#define FAMILY_LINKAGE_SCHEMA_VERSION 1u

// Bounds for a device group and the maximum members of a group.
#define FAMILY_MIN_GROUP_MEMBERS 1u
#define FAMILY_MAX_GROUP_MEMBERS 16u

// Bounds for the content-sync retry budget and backoff in seconds.
#define FAMILY_MIN_SYNC_RETRY_SECONDS 5u
#define FAMILY_MAX_SYNC_RETRY_SECONDS 3600u
#define FAMILY_MAX_SYNC_ATTEMPTS 8u

// One message routing scope shared with the platform.
typedef enum {
    ROUTE_SCOPE_DEVICE = 0,
    ROUTE_SCOPE_GROUP,
    ROUTE_SCOPE_FAMILY,
} route_scope_t;

// Conflict resolution strategy shared with the platform. A higher version
// always wins; the strategy only breaks a tie at the same version.
typedef enum {
    CONFLICT_LOCAL_WINS = 0,
    CONFLICT_REMOTE_WINS,
    CONFLICT_DEFER,
} conflict_strategy_t;

// Content-sync outcome shared with the platform.
typedef enum {
    SYNC_STATE_IDLE = 0,
    SYNC_STATE_PENDING,
    SYNC_STATE_APPLIED,
    SYNC_STATE_CONFLICT,
    SYNC_STATE_FAILED,
} sync_state_t;

// One bounded family-linkage policy.
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t group_members;
    uint8_t conflict_strategy;
    uint8_t reserved;
    uint16_t sync_retry_seconds;
    uint16_t sync_max_attempts;
} family_policy_t;

// One content-sync version pair used to resolve a conflict.
typedef struct {
    uint64_t local_version;
    uint64_t remote_version;
} sync_version_pair_t;

// Stable error codes for diagnostics and guardian-facing copy.
typedef enum {
    FAMILY_OK = 0,
    FAMILY_ERR_NULL_ARGUMENT,
    FAMILY_ERR_INVALID_POLICY,
    FAMILY_ERR_INVALID_ROUTE,
    FAMILY_ERR_INVALID_STATE,
    FAMILY_ERR_STORAGE,
} family_error_t;

// Return the conservative default policy: enabled, a single member and the
// shortest retry window so a lone device behaves like a family of one.
family_policy_t family_default(void);

// Clamp one policy into the documented bounds.
family_policy_t family_clamp(const family_policy_t *policy);

// Return true when a policy is version-compatible and bounded.
bool family_is_valid(const family_policy_t *policy);

// Return the stable string for one family error code.
const char *family_error_name(family_error_t error);

// Return the stable string for one route scope.
const char *route_scope_name(route_scope_t scope);

// Return the stable string for one conflict strategy.
const char *conflict_strategy_name(conflict_strategy_t strategy);

// Return the stable string for one sync state.
const char *sync_state_name(sync_state_t state);

// Return true when a route to the given scope is allowed. A device-scoped
// route is always allowed; a group route needs a group and a family route needs
// the linkage to be enabled so a disabled device never broadcasts to the
// family.
bool family_route_allowed(const family_policy_t *policy, route_scope_t scope);

// Resolve a content conflict from a version pair and the configured strategy.
// A higher version always wins; the strategy only breaks a same-version tie.
conflict_strategy_t family_resolve_conflict(
    const family_policy_t *policy,
    sync_version_pair_t versions
);

// Return the sync state for a version pair. Equal versions are already
// consistent, a newer version applies, and a same-version tie follows the
// conflict strategy.
sync_state_t family_sync_state(
    const family_policy_t *policy,
    sync_version_pair_t versions
);

// Return true when the retry window has elapsed. The comparison is inclusive
// so a retry exactly at the bound fires in host tests and on the device alike.
bool family_sync_should_retry(
    const family_policy_t *policy,
    uint32_t elapsed_ms
);

// Initialize the family-linkage store.
esp_err_t family_init(void);

// Return true when the family-linkage store is ready.
bool family_is_ready(void);

// Store a guardian policy, clamping out-of-range values.
esp_err_t family_set(const family_policy_t *policy);

// Return the currently applied policy.
family_policy_t family_get(void);

// Apply a policy that is already version-compatible and bounded.
esp_err_t family_apply(const family_policy_t *policy);

// Record one content-sync attempt and return the resulting state.
sync_state_t family_record_sync(sync_version_pair_t versions);

// Return the last recorded sync state.
sync_state_t family_last_sync_state(void);

// Return true when the retry budget for the last failure is exhausted.
bool family_sync_exhausted(void);

// Return the removable-module descriptor for family_linkage.
const module_descriptor_t *family_linkage_module_descriptor(void);

#ifdef __cplusplus
}
#endif
