#include "battery_core.h"

#include <stddef.h>
#include <string.h>

static uint8_t clamp_u8(uint32_t value, uint32_t minimum, uint32_t maximum) {
    if (value < minimum) {
        return (uint8_t)minimum;
    }
    if (value > maximum) {
        return (uint8_t)maximum;
    }
    return (uint8_t)value;
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

battery_policy_t battery_default(void) {
    const battery_policy_t policy = {
        .schema_version = BATTERY_SCHEMA_VERSION,
        .enabled = 1u,
        .low_power_enabled = 1u,
        .deep_sleep_enabled = 1u,
        .charge_priority = 1u,
        .low_battery_warn_percent = 20u,
        .low_power_enter_percent = 15u,
        .sleep_after_seconds = 1800u,
        .revision = 0u,
    };
    return policy;
}

battery_policy_t battery_clamp(const battery_policy_t *policy) {
    battery_policy_t clamped = battery_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.low_power_enabled = policy->low_power_enabled != 0u ? 1u : 0u;
    clamped.deep_sleep_enabled = policy->deep_sleep_enabled != 0u ? 1u : 0u;
    clamped.charge_priority = policy->charge_priority != 0u ? 1u : 0u;
    clamped.low_battery_warn_percent = clamp_u8(
        policy->low_battery_warn_percent,
        BATTERY_MIN_WARN_PERCENT,
        BATTERY_MAX_WARN_PERCENT
    );
    clamped.low_power_enter_percent = clamp_u8(
        policy->low_power_enter_percent,
        BATTERY_MIN_LOW_POWER_PERCENT,
        BATTERY_MAX_LOW_POWER_PERCENT
    );
    clamped.sleep_after_seconds = clamp_u32(
        policy->sleep_after_seconds,
        BATTERY_MIN_SLEEP_AFTER_SECONDS,
        BATTERY_MAX_SLEEP_AFTER_SECONDS
    );
    clamped.revision = policy->revision;
    clamped.schema_version = BATTERY_SCHEMA_VERSION;
    return clamped;
}

bool battery_is_valid(const battery_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != BATTERY_SCHEMA_VERSION) {
        return false;
    }
    if (policy->low_battery_warn_percent < BATTERY_MIN_WARN_PERCENT ||
        policy->low_battery_warn_percent > BATTERY_MAX_WARN_PERCENT) {
        return false;
    }
    if (policy->low_power_enter_percent < BATTERY_MIN_LOW_POWER_PERCENT ||
        policy->low_power_enter_percent > BATTERY_MAX_LOW_POWER_PERCENT) {
        return false;
    }
    if (policy->sleep_after_seconds < BATTERY_MIN_SLEEP_AFTER_SECONDS ||
        policy->sleep_after_seconds > BATTERY_MAX_SLEEP_AFTER_SECONDS) {
        return false;
    }
    return true;
}

battery_power_mode_t battery_decide_mode(
    const battery_policy_t *policy,
    const battery_snapshot_t *snapshot,
    bool idle_elapsed
) {
    if (policy == NULL || snapshot == NULL) {
        return BATTERY_MODE_NORMAL;
    }
    const battery_policy_t bounded = battery_clamp(policy);
    if (bounded.enabled == 0u || !snapshot->has_measurement) {
        return BATTERY_MODE_NORMAL;
    }
    // A charger keeps the device fully awake; only discharge can throttle.
    if (snapshot->charge_state == BATTERY_CHARGE_CHARGING ||
        snapshot->charge_state == BATTERY_CHARGE_FULL) {
        return BATTERY_MODE_NORMAL;
    }
    if (bounded.deep_sleep_enabled != 0u && idle_elapsed &&
        snapshot->percent <= bounded.low_power_enter_percent) {
        return BATTERY_MODE_DEEP_SLEEP;
    }
    if (bounded.low_power_enabled != 0u &&
        snapshot->percent <= bounded.low_power_enter_percent) {
        return BATTERY_MODE_LOW_POWER;
    }
    return BATTERY_MODE_NORMAL;
}

bool battery_low_warning(
    const battery_policy_t *policy,
    const battery_snapshot_t *snapshot
) {
    if (policy == NULL || snapshot == NULL || !snapshot->has_measurement) {
        return false;
    }
    return snapshot->percent <= policy->low_battery_warn_percent;
}

bool battery_wake_source_retained(
    const battery_policy_t *policy,
    battery_power_mode_t mode
) {
    if (policy == NULL || policy->enabled == 0u) {
        return false;
    }
    // Deep sleep always keeps at least one wake source so the device can be
    // woken by the retained trigger; low power keeps every normal source.
    switch (mode) {
        case BATTERY_MODE_DEEP_SLEEP:
            return policy->deep_sleep_enabled != 0u;
        case BATTERY_MODE_LOW_POWER:
            return policy->low_power_enabled != 0u;
        case BATTERY_MODE_NORMAL:
        default:
            return true;
    }
}

void battery_average_init(battery_average_t *average) {
    if (average == NULL) {
        return;
    }
    memset(average, 0, sizeof(*average));
}

uint8_t battery_average_add(battery_average_t *average, uint8_t percent) {
    if (average == NULL) {
        return percent;
    }
    average->sample_count += 1u;
    average->sample_sum += percent;
    const uint32_t rounded =
        (average->sample_sum + average->sample_count / 2u) /
        average->sample_count;
    return clamp_u8(rounded, 0u, 100u);
}

battery_charge_state_t battery_derive_charge_state(
    bool charging,
    bool full
) {
    if (!charging) {
        return BATTERY_CHARGE_DISCHARGING;
    }
    return full ? BATTERY_CHARGE_FULL : BATTERY_CHARGE_CHARGING;
}
