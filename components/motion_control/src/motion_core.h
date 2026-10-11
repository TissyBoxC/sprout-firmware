#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "motion_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pure safety helpers shared by the host tests and the device loop.
 *
 * The controller keeps the queue and the lifecycle in the runtime file; these
 * helpers encode the bounded arithmetic so the safety envelope is testable
 * without any hardware.
 */

/** @brief Return true when an actuator index is addressable. */
bool motion_actuator_valid(uint8_t actuator);

/** @brief Clamp an angle into the documented degree range. */
int16_t motion_clamp_angle(int16_t angle_degrees);

/**
 * @brief Clamp a command into the policy bounds as a whole.
 *
 * The returned command is safe to execute: the actuator index is validated
 * separately with `motion_actuator_valid`, the angle is clamped and the speed
 * is clamped to the effective policy maximum.
 */
motion_command_t motion_clamp_command(
    const motion_policy_t *policy,
    const motion_command_t *command
);

/** @brief Return true when an action requires the locomotion permission. */
bool motion_action_is_locomotion(motion_action_t action);

/**
 * @brief Return true when a bounded run time has elapsed.
 *
 * The comparison is inclusive so a run time exactly at the bound trips the
 * timeout in host tests and on the device alike.
 */
bool motion_runtime_exceeded(
    const motion_policy_t *policy,
    uint32_t elapsed_ms
);

#ifdef __cplusplus
}
#endif
