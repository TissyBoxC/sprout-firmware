#include "display_policy.h"
#include "display_policy_core.h"

#include <cassert>

int main() {
    const display_policy_t defaults = display_policy_default();
    assert(display_policy_is_valid(&defaults));
    assert(defaults.brightness_percent == 60u);
    assert(defaults.break_interval_minutes == 20u);

    display_policy_t out_of_range = defaults;
    out_of_range.brightness_percent = 0u;
    out_of_range.color_temperature_kelvin = 9000u;
    out_of_range.daily_limit_minutes = 1u;
    out_of_range.dim_after_seconds = 1u;
    const display_policy_t clamped =
        display_policy_clamp(&out_of_range);
    assert(clamped.brightness_percent == DISPLAY_POLICY_MIN_BRIGHTNESS);
    assert(clamped.color_temperature_kelvin ==
           DISPLAY_POLICY_MAX_COLOR_TEMPERATURE);
    assert(clamped.daily_limit_minutes ==
           DISPLAY_POLICY_MIN_DAILY_LIMIT_MINUTES);
    assert(clamped.dim_after_seconds ==
           DISPLAY_POLICY_MIN_DIM_AFTER_SECONDS);
    assert(display_policy_is_valid(&clamped));

    assert(!display_policy_is_valid(nullptr));
    display_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!display_policy_is_valid(&wrong_version));
    assert(display_policy_clamp(nullptr).brightness_percent ==
           defaults.brightness_percent);

    display_policy_usage_t usage;
    display_policy_usage_init(&usage);
    const uint32_t base = 1700000000u;
    display_policy_usage_start_break_window(&usage, base);
    assert(display_policy_usage_break_due(&usage, 20u, base + 1199u) ==
           false);
    assert(display_policy_usage_break_due(&usage, 20u, base + 1200u) ==
           true);

    for (uint32_t second = 0; second < 60u; ++second) {
        display_policy_usage_add_second(&usage, base + second);
    }
    assert(usage.used_seconds == 60u);
    assert(display_policy_usage_daily_limit_reached(
        &usage,
        1u,
        base + 60u
    ));
    // Crossing into a new UTC day resets the daily total.
    display_policy_usage_add_second(&usage, base + 86400u);
    assert(usage.used_seconds == 1u);
    assert(!display_policy_usage_daily_limit_reached(
        &usage,
        600u,
        base + 86400u
    ));
    return 0;
}
