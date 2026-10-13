#include "display_policy_sync_core.h"

static uint32_t to_u32(int value) {
    return value < 0 ? 0u : (uint32_t)value;
}

display_policy_t display_policy_sync_core_build(
    int brightness_percent,
    int color_temperature_kelvin,
    int min_view_distance_cm,
    int daily_limit_minutes,
    int break_interval_minutes,
    int dim_after_seconds,
    bool auto_brightness,
    bool night_light,
    bool distance_reminder,
    int revision
) {
    display_policy_t policy = display_policy_default();
    policy.brightness_percent = (uint8_t)to_u32(brightness_percent);
    policy.color_temperature_kelvin =
        (uint16_t)to_u32(color_temperature_kelvin);
    policy.min_view_distance_cm = (uint16_t)to_u32(min_view_distance_cm);
    policy.daily_limit_minutes = (uint16_t)to_u32(daily_limit_minutes);
    policy.break_interval_minutes = (uint16_t)to_u32(break_interval_minutes);
    policy.dim_after_seconds = (uint16_t)to_u32(dim_after_seconds);
    policy.auto_brightness = auto_brightness ? 1u : 0u;
    policy.night_light = night_light ? 1u : 0u;
    policy.distance_reminder = distance_reminder ? 1u : 0u;
    policy.revision = to_u32(revision);
    // The clamp pins every field into the shared bounds and restamps the
    // schema version, so a malformed platform value can never reach the panel.
    return display_policy_clamp(&policy);
}
