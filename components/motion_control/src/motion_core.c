#include "motion_core.h"

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

bool motion_actuator_valid(uint8_t actuator) {
    return actuator < MOTION_MAX_ACTUATORS;
}

int16_t motion_clamp_angle(int16_t angle_degrees) {
    if (angle_degrees < MOTION_MIN_ANGLE_DEGREES) {
        return MOTION_MIN_ANGLE_DEGREES;
    }
    if (angle_degrees > MOTION_MAX_ANGLE_DEGREES) {
        return MOTION_MAX_ANGLE_DEGREES;
    }
    return angle_degrees;
}

bool motion_action_is_locomotion(motion_action_t action) {
    return action == MOTION_ACTION_WALK || action == MOTION_ACTION_DANCE;
}

motion_command_t motion_clamp_command(
    const motion_policy_t *policy,
    const motion_command_t *command
) {
    motion_command_t clamped = {
        MOTION_ACTION_IDLE,
        MOTION_ACTION_IDLE,
        MOTION_MIN_ANGLE_DEGREES,
        MOTION_MIN_SPEED_PERCENT,
    };
    if (command == NULL) {
        return clamped;
    }
    clamped.actuator = command->actuator;
    clamped.action = command->action;
    clamped.angle_degrees = motion_clamp_angle(command->angle_degrees);
    uint32_t maximum = MOTION_MAX_SPEED_PERCENT;
    if (policy != NULL) {
        const motion_policy_t bounded = motion_clamp(policy);
        maximum = bounded.max_speed_percent;
    }
    clamped.speed_percent = clamp_u8(
        command->speed_percent,
        MOTION_MIN_SPEED_PERCENT,
        maximum
    );
    return clamped;
}

bool motion_runtime_exceeded(
    const motion_policy_t *policy,
    uint32_t elapsed_ms
) {
    const motion_policy_t bounded = motion_clamp(policy);
    return elapsed_ms >= bounded.max_runtime_ms;
}

motion_policy_t motion_default(void) {
    const motion_policy_t policy = {
        .schema_version = MOTION_SCHEMA_VERSION,
        .enabled = 1u,
        .require_stop_ack = 1u,
        .allow_locomotion = 0u,
        .reserved = 0u,
        .max_speed_percent = 60u,
        .max_runtime_ms = 5000u,
        .stop_grace_ms = 300u,
    };
    return policy;
}

motion_policy_t motion_clamp(const motion_policy_t *policy) {
    motion_policy_t clamped = motion_default();
    if (policy == NULL) {
        return clamped;
    }
    clamped.enabled = policy->enabled != 0u ? 1u : 0u;
    clamped.require_stop_ack = policy->require_stop_ack != 0u ? 1u : 0u;
    clamped.allow_locomotion = policy->allow_locomotion != 0u ? 1u : 0u;
    clamped.reserved = 0u;
    clamped.max_speed_percent = clamp_u8(
        policy->max_speed_percent,
        MOTION_MIN_SPEED_PERCENT,
        MOTION_MAX_SPEED_PERCENT
    );
    clamped.max_runtime_ms = clamp_u16(
        policy->max_runtime_ms,
        MOTION_MIN_MAX_RUNTIME_MS,
        MOTION_MAX_MAX_RUNTIME_MS
    );
    clamped.stop_grace_ms = clamp_u16(
        policy->stop_grace_ms,
        0u,
        MOTION_MAX_MAX_RUNTIME_MS
    );
    clamped.schema_version = MOTION_SCHEMA_VERSION;
    return clamped;
}

bool motion_is_valid(const motion_policy_t *policy) {
    if (policy == NULL) {
        return false;
    }
    if (policy->schema_version != MOTION_SCHEMA_VERSION) {
        return false;
    }
    if (policy->max_speed_percent < MOTION_MIN_SPEED_PERCENT ||
        policy->max_speed_percent > MOTION_MAX_SPEED_PERCENT) {
        return false;
    }
    if (policy->max_runtime_ms < MOTION_MIN_MAX_RUNTIME_MS ||
        policy->max_runtime_ms > MOTION_MAX_MAX_RUNTIME_MS) {
        return false;
    }
    if (policy->stop_grace_ms > MOTION_MAX_MAX_RUNTIME_MS) {
        return false;
    }
    return true;
}
