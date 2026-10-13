#include "power_policy_sync_core.h"

#include <cassert>

int main() {
    // A well-formed platform policy maps onto the firmware contract.
    const battery_policy_t policy = power_policy_sync_core_build(
        true,
        false,
        true,
        false,
        30,
        10,
        1200,
        7
    );
    assert(policy.schema_version == BATTERY_SCHEMA_VERSION);
    assert(policy.enabled == 1u);
    assert(policy.low_power_enabled == 0u);
    assert(policy.deep_sleep_enabled == 1u);
    assert(policy.charge_priority == 0u);
    assert(policy.low_battery_warn_percent == 30u);
    assert(policy.low_power_enter_percent == 10u);
    assert(policy.sleep_after_seconds == 1200u);
    assert(policy.revision == 7u);
    assert(battery_is_valid(&policy));

    // Out-of-range values are clamped instead of installed verbatim, and a
    // negative revision is treated as zero.
    const battery_policy_t clamped = power_policy_sync_core_build(
        false,
        true,
        true,
        true,
        999,
        1,
        1,
        -4
    );
    assert(clamped.low_battery_warn_percent == BATTERY_MAX_WARN_PERCENT);
    assert(clamped.low_power_enter_percent == BATTERY_MIN_LOW_POWER_PERCENT);
    assert(clamped.sleep_after_seconds == BATTERY_MIN_SLEEP_AFTER_SECONDS);
    assert(clamped.revision == 0u);
    assert(battery_is_valid(&clamped));

    return 0;
}
