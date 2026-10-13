#include "display_policy_core.h"

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

display_policy_t display_policy_default(void) {
    const display_policy_t policy = {
        .schema_version = DISPLAY_POLICY_SCHEMA_VERSION,
        .brightness_percent = 60u,
        .color_temperature_kelvin = 4500u,
        .min_view_distance_cm = 40u,
        .daily_limit_minutes = 60u,
        .break_interval_minutes = 20u,
        .dim_after_seconds = 60u,
        .auto_brightness = 1u,
        .night_light = 1u,
        .distance_reminder = 1u,
        .revision = 0u,
    };
    return policy;
}

display_policy_t display_policy_clamp(const display_policy_t *policy) {
    display_policy_t clamped = display_policy_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.brightness_percent = clamp_u8(
        policy->brightness_percent,
        DISPLAY_POLICY_MIN_BRIGHTNESS,
        DISPLAY_POLICY_MAX_BRIGHTNESS
    );
    clamped.color_temperature_kelvin = clamp_u16(
        policy->color_temperature_kelvin,
        DISPLAY_POLICY_MIN_COLOR_TEMPERATURE,
        DISPLAY_POLICY_MAX_COLOR_TEMPERATURE
    );
    clamped.min_view_distance_cm = clamp_u16(
        policy->min_view_distance_cm,
        DISPLAY_POLICY_MIN_VIEW_DISTANCE_CM,
        DISPLAY_POLICY_MAX_VIEW_DISTANCE_CM
    );
    clamped.daily_limit_minutes = clamp_u16(
        policy->daily_limit_minutes,
        DISPLAY_POLICY_MIN_DAILY_LIMIT_MINUTES,
        DISPLAY_POLICY_MAX_DAILY_LIMIT_MINUTES
    );
    clamped.break_interval_minutes = clamp_u16(
        policy->break_interval_minutes,
        DISPLAY_POLICY_MIN_BREAK_MINUTES,
        DISPLAY_POLICY_MAX_BREAK_MINUTES
    );
    clamped.dim_after_seconds = clamp_u16(
        policy->dim_after_seconds,
        DISPLAY_POLICY_MIN_DIM_AFTER_SECONDS,
        DISPLAY_POLICY_MAX_DIM_AFTER_SECONDS
    );
    clamped.auto_brightness = policy->auto_brightness != 0u ? 1u : 0u;
    clamped.night_light = policy->night_light != 0u ? 1u : 0u;
    clamped.distance_reminder = policy->distance_reminder != 0u ? 1u : 0u;
    clamped.revision = policy->revision;
    clamped.schema_version = DISPLAY_POLICY_SCHEMA_VERSION;
    return clamped;
}

bool display_policy_is_valid(const display_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != DISPLAY_POLICY_SCHEMA_VERSION) {
        return false;
    }
    if (policy->brightness_percent < DISPLAY_POLICY_MIN_BRIGHTNESS ||
        policy->brightness_percent > DISPLAY_POLICY_MAX_BRIGHTNESS) {
        return false;
    }
    if (policy->color_temperature_kelvin <
            DISPLAY_POLICY_MIN_COLOR_TEMPERATURE ||
        policy->color_temperature_kelvin >
            DISPLAY_POLICY_MAX_COLOR_TEMPERATURE) {
        return false;
    }
    if (policy->min_view_distance_cm < DISPLAY_POLICY_MIN_VIEW_DISTANCE_CM ||
        policy->min_view_distance_cm > DISPLAY_POLICY_MAX_VIEW_DISTANCE_CM) {
        return false;
    }
    if (policy->daily_limit_minutes < DISPLAY_POLICY_MIN_DAILY_LIMIT_MINUTES ||
        policy->daily_limit_minutes > DISPLAY_POLICY_MAX_DAILY_LIMIT_MINUTES) {
        return false;
    }
    if (policy->break_interval_minutes < DISPLAY_POLICY_MIN_BREAK_MINUTES ||
        policy->break_interval_minutes > DISPLAY_POLICY_MAX_BREAK_MINUTES) {
        return false;
    }
    if (policy->dim_after_seconds < DISPLAY_POLICY_MIN_DIM_AFTER_SECONDS ||
        policy->dim_after_seconds > DISPLAY_POLICY_MAX_DIM_AFTER_SECONDS) {
        return false;
    }
    return true;
}

uint32_t display_policy_day_key(uint32_t epoch_seconds) {
    // 86400 seconds per UTC day; the key is a bounded rolling counter.
    return epoch_seconds / 86400u;
}

void display_policy_usage_init(display_policy_usage_t *usage) {
    if (usage == NULL) {
        return;
    }
    memset(usage, 0, sizeof(*usage));
}

void display_policy_usage_add_second(
    display_policy_usage_t *usage,
    uint32_t now_epoch_seconds
) {
    if (usage == NULL) {
        return;
    }
    const uint32_t day_key = display_policy_day_key(now_epoch_seconds);
    if (!usage->break_window_active || usage->day_key != day_key) {
        usage->day_key = day_key;
        if (usage->break_window_active) {
            usage->used_seconds = 0u;
        }
    }
    usage->day_key = day_key;
    usage->used_seconds += 1u;
}

void display_policy_usage_start_break_window(
    display_policy_usage_t *usage,
    uint32_t now_epoch_seconds
) {
    if (usage == NULL) {
        return;
    }
    usage->day_key = display_policy_day_key(now_epoch_seconds);
    usage->break_window_started_at = now_epoch_seconds;
    usage->break_window_active = true;
}

bool display_policy_usage_break_due(
    const display_policy_usage_t *usage,
    uint16_t break_interval_minutes,
    uint32_t now_epoch_seconds
) {
    if (usage == NULL || !usage->break_window_active) {
        return false;
    }
    const uint32_t interval_seconds =
        (uint32_t)break_interval_minutes * 60u;
    return now_epoch_seconds >= usage->break_window_started_at &&
        now_epoch_seconds - usage->break_window_started_at >= interval_seconds;
}

bool display_policy_usage_daily_limit_reached(
    const display_policy_usage_t *usage,
    uint16_t daily_limit_minutes,
    uint32_t now_epoch_seconds
) {
    if (usage == NULL) {
        return false;
    }
    if (usage->day_key != display_policy_day_key(now_epoch_seconds)) {
        return false;
    }
    const uint32_t limit_seconds = (uint32_t)daily_limit_minutes * 60u;
    return usage->used_seconds >= limit_seconds;
}
