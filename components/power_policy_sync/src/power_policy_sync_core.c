#include "power_policy_sync_core.h"

static uint32_t to_u32(int value) {
    return value < 0 ? 0u : (uint32_t)value;
}

battery_policy_t power_policy_sync_core_build(
    bool enabled,
    bool low_power_enabled,
    bool deep_sleep_enabled,
    bool charge_priority,
    int low_battery_warn_percent,
    int low_power_enter_percent,
    int sleep_after_seconds,
    int revision
) {
    battery_policy_t policy = battery_default();
    policy.enabled = enabled ? 1u : 0u;
    policy.low_power_enabled = low_power_enabled ? 1u : 0u;
    policy.deep_sleep_enabled = deep_sleep_enabled ? 1u : 0u;
    policy.charge_priority = charge_priority ? 1u : 0u;
    policy.low_battery_warn_percent =
        (uint8_t)to_u32(low_battery_warn_percent);
    policy.low_power_enter_percent = (uint8_t)to_u32(low_power_enter_percent);
    policy.sleep_after_seconds = to_u32(sleep_after_seconds);
    policy.revision = to_u32(revision);
    // The clamp pins every field into the shared bounds and restamps the
    // schema version, so a malformed platform value can never reach the
    // power manager.
    return battery_clamp(&policy);
}
