#include "family_linkage.h"

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

family_policy_t family_default(void) {
    const family_policy_t policy = {
        .schema_version = FAMILY_LINKAGE_SCHEMA_VERSION,
        .enabled = 1u,
        // A lone device is a valid family of one, so the linkage starts usable
        // without a second device.
        .group_members = 1u,
        .conflict_strategy = (uint8_t)CONFLICT_REMOTE_WINS,
        .reserved = 0u,
        .sync_retry_seconds = 30u,
        .sync_max_attempts = 4u,
    };
    return policy;
}

family_policy_t family_clamp(const family_policy_t *policy) {
    family_policy_t clamped = family_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.schema_version = FAMILY_LINKAGE_SCHEMA_VERSION;
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.group_members = clamp_u8(
        policy->group_members,
        FAMILY_MIN_GROUP_MEMBERS,
        FAMILY_MAX_GROUP_MEMBERS
    );
    clamped.conflict_strategy = clamp_u8(
        policy->conflict_strategy,
        (uint32_t)CONFLICT_LOCAL_WINS,
        (uint32_t)CONFLICT_DEFER
    );
    clamped.reserved = 0u;
    clamped.sync_retry_seconds = clamp_u16(
        policy->sync_retry_seconds,
        FAMILY_MIN_SYNC_RETRY_SECONDS,
        FAMILY_MAX_SYNC_RETRY_SECONDS
    );
    clamped.sync_max_attempts = clamp_u8(
        policy->sync_max_attempts,
        1u,
        FAMILY_MAX_SYNC_ATTEMPTS
    );
    return clamped;
}

bool family_is_valid(const family_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != FAMILY_LINKAGE_SCHEMA_VERSION) {
        return false;
    }
    if (policy->group_members < FAMILY_MIN_GROUP_MEMBERS ||
        policy->group_members > FAMILY_MAX_GROUP_MEMBERS) {
        return false;
    }
    if (policy->conflict_strategy > (uint8_t)CONFLICT_DEFER) {
        return false;
    }
    if (policy->sync_retry_seconds < FAMILY_MIN_SYNC_RETRY_SECONDS ||
        policy->sync_retry_seconds > FAMILY_MAX_SYNC_RETRY_SECONDS) {
        return false;
    }
    // Widen the uint8 field before comparing so the bound stays a real runtime
    // comparison rather than a compile-time constant fold.
    const uint32_t max_attempts = policy->sync_max_attempts;
    if (max_attempts < 1u || max_attempts > FAMILY_MAX_SYNC_ATTEMPTS) {
        return false;
    }
    return true;
}

bool family_route_allowed(
    const family_policy_t *policy,
    route_scope_t scope
) {
    if (policy == NULL) {
        return false;
    }
    const family_policy_t bounded = family_clamp(policy);
    switch (scope) {
        case ROUTE_SCOPE_DEVICE:
            // A device-scoped route only needs the device itself, so it is
            // always allowed even when the linkage is disabled.
            return true;
        case ROUTE_SCOPE_GROUP:
            return bounded.enabled != 0u && bounded.group_members > 1u;
        case ROUTE_SCOPE_FAMILY:
            return bounded.enabled != 0u;
        default:
            return false;
    }
}

conflict_strategy_t family_resolve_conflict(
    const family_policy_t *policy,
    sync_version_pair_t versions
) {
    if (versions.local_version > versions.remote_version) {
        return CONFLICT_LOCAL_WINS;
    }
    if (versions.remote_version > versions.local_version) {
        return CONFLICT_REMOTE_WINS;
    }
    // A same-version tie follows the configured strategy.
    const family_policy_t bounded = family_clamp(policy);
    return (conflict_strategy_t)bounded.conflict_strategy;
}

sync_state_t family_sync_state(
    const family_policy_t *policy,
    sync_version_pair_t versions
) {
    if (versions.local_version == versions.remote_version) {
        // At an equal version the strategy decides: a deterministic tie-break
        // is already consistent, while a defer hands the same-version tie to a
        // human as a conflict instead of silently applying it.
        const conflict_strategy_t resolved =
            family_resolve_conflict(policy, versions);
        return resolved == CONFLICT_DEFER
            ? SYNC_STATE_CONFLICT
            : SYNC_STATE_APPLIED;
    }
    const conflict_strategy_t resolved =
        family_resolve_conflict(policy, versions);
    if (resolved == CONFLICT_DEFER) {
        return SYNC_STATE_CONFLICT;
    }
    return SYNC_STATE_APPLIED;
}

bool family_sync_should_retry(
    const family_policy_t *policy,
    uint32_t elapsed_ms
) {
    const family_policy_t bounded = family_clamp(policy);
    const uint32_t limit_ms = (uint32_t)bounded.sync_retry_seconds * 1000u;
    return elapsed_ms >= limit_ms;
}

const char *family_error_name(family_error_t error) {
    switch (error) {
        case FAMILY_OK:
            return "ok";
        case FAMILY_ERR_NULL_ARGUMENT:
            return "null_argument";
        case FAMILY_ERR_INVALID_POLICY:
            return "invalid_policy";
        case FAMILY_ERR_INVALID_ROUTE:
            return "invalid_route";
        case FAMILY_ERR_INVALID_STATE:
            return "invalid_state";
        case FAMILY_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

const char *route_scope_name(route_scope_t scope) {
    switch (scope) {
        case ROUTE_SCOPE_GROUP:
            return "group";
        case ROUTE_SCOPE_FAMILY:
            return "family";
        case ROUTE_SCOPE_DEVICE:
        default:
            return "device";
    }
}

const char *conflict_strategy_name(conflict_strategy_t strategy) {
    switch (strategy) {
        case CONFLICT_REMOTE_WINS:
            return "remote_wins";
        case CONFLICT_DEFER:
            return "defer";
        case CONFLICT_LOCAL_WINS:
        default:
            return "local_wins";
    }
}

const char *sync_state_name(sync_state_t state) {
    switch (state) {
        case SYNC_STATE_PENDING:
            return "pending";
        case SYNC_STATE_APPLIED:
            return "applied";
        case SYNC_STATE_CONFLICT:
            return "conflict";
        case SYNC_STATE_FAILED:
            return "failed";
        case SYNC_STATE_IDLE:
        default:
            return "idle";
    }
}
