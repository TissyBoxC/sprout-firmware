#include "motion_policy_sync_core.h"

static uint32_t to_u32(int value) {
    return value < 0 ? 0u : (uint32_t)value;
}

// Clamp an int platform value into a bounded range before narrowing, so a
// value wider than the target field cannot wrap into an in-range number.
static uint8_t clamp_int_to_u8(int value, int minimum, int maximum) {
    if (value < minimum) {
        value = minimum;
    }
    if (value > maximum) {
        value = maximum;
    }
    return (uint8_t)value;
}

// Clamp an int platform value into a bounded range before narrowing, so a
// value wider than the target field cannot wrap into an in-range number.
static uint16_t clamp_int_to_u16(int value, int minimum, int maximum) {
    if (value < minimum) {
        value = minimum;
    }
    if (value > maximum) {
        value = maximum;
    }
    return (uint16_t)value;
}

motion_policy_t motion_policy_sync_core_build(
    bool enabled,
    bool require_stop_ack,
    bool allow_locomotion,
    int max_speed_percent,
    int max_runtime_ms,
    int stop_grace_ms,
    int revision
) {
    motion_policy_t policy = motion_default();
    policy.enabled = enabled ? 1u : 0u;
    policy.require_stop_ack = require_stop_ack ? 1u : 0u;
    policy.allow_locomotion = allow_locomotion ? 1u : 0u;
    policy.max_speed_percent = clamp_int_to_u8(
        max_speed_percent,
        (int)MOTION_MIN_SPEED_PERCENT,
        (int)MOTION_MAX_SPEED_PERCENT
    );
    policy.max_runtime_ms = clamp_int_to_u16(
        max_runtime_ms,
        (int)MOTION_MIN_MAX_RUNTIME_MS,
        (int)MOTION_MAX_MAX_RUNTIME_MS
    );
    policy.stop_grace_ms = clamp_int_to_u16(
        stop_grace_ms,
        0,
        (int)MOTION_MAX_MAX_RUNTIME_MS
    );
    policy.revision = to_u32(revision);
    // The clamp pins every field into the shared bounds and restamps the
    // schema version, so a malformed platform value can never reach the
    // motion controller.
    return motion_clamp(&policy);
}
