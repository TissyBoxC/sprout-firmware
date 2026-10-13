#include "display_policy_sync_core.h"

#include <cassert>

int main() {
    // A well-formed platform policy maps onto the firmware contract.
    const display_policy_t policy = display_policy_sync_core_build(
        45,
        3200,
        50,
        90,
        25,
        120,
        true,
        false,
        true,
        7
    );
    assert(policy.schema_version == DISPLAY_POLICY_SCHEMA_VERSION);
    assert(policy.brightness_percent == 45u);
    assert(policy.color_temperature_kelvin == 3200u);
    assert(policy.min_view_distance_cm == 50u);
    assert(policy.daily_limit_minutes == 90u);
    assert(policy.break_interval_minutes == 25u);
    assert(policy.dim_after_seconds == 120u);
    assert(policy.auto_brightness == 1u);
    assert(policy.night_light == 0u);
    assert(policy.distance_reminder == 1u);
    assert(policy.revision == 7u);

    // Out-of-range values are clamped instead of installed verbatim.
    const display_policy_t clamped = display_policy_sync_core_build(
        999,
        100,
        0,
        1,
        10000,
        1,
        false,
        true,
        false,
        -5
    );
    assert(clamped.brightness_percent == DISPLAY_POLICY_MAX_BRIGHTNESS);
    assert(clamped.color_temperature_kelvin ==
           DISPLAY_POLICY_MIN_COLOR_TEMPERATURE);
    assert(clamped.min_view_distance_cm == DISPLAY_POLICY_MIN_VIEW_DISTANCE_CM);
    assert(clamped.daily_limit_minutes == DISPLAY_POLICY_MIN_DAILY_LIMIT_MINUTES);
    assert(clamped.break_interval_minutes == DISPLAY_POLICY_MAX_BREAK_MINUTES);
    assert(clamped.dim_after_seconds == DISPLAY_POLICY_MIN_DIM_AFTER_SECONDS);
    assert(clamped.revision == 0u);
    assert(display_policy_is_valid(&clamped));

    return 0;
}
