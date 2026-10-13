#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "module_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Stable motion policy schema version shared with the platform. */
#define MOTION_SCHEMA_VERSION 1u

/** @brief Number of independently driven actuator channels. */
#define MOTION_MAX_ACTUATORS 8u

/** @brief Bounds for the maximum continuous run time in milliseconds. */
#define MOTION_MIN_MAX_RUNTIME_MS 200u
#define MOTION_MAX_MAX_RUNTIME_MS 60000u

/** @brief Bounds for the bounded actuator speed in percent. */
#define MOTION_MIN_SPEED_PERCENT 1u
#define MOTION_MAX_SPEED_PERCENT 100u

/** @brief Bounds for one bounded actuator target position in degrees. */
#define MOTION_MIN_ANGLE_DEGREES 0
#define MOTION_MAX_ANGLE_DEGREES 180

/** @brief The actions the motion controller can run on a body. */
typedef enum {
    MOTION_ACTION_IDLE = 0,
    MOTION_ACTION_WALK,
    MOTION_ACTION_DANCE,
    MOTION_ACTION_TAIL_WAG,
    MOTION_ACTION_GESTURE,
} motion_action_t;

/** @brief The normalized lifecycle state of the motion controller. */
typedef enum {
    MOTION_STATE_IDLE = 0,
    MOTION_STATE_RUNNING,
    MOTION_STATE_STOPPED,
    MOTION_STATE_TIMEOUT,
    MOTION_STATE_FAULT,
} motion_state_t;

/** @brief One bounded guarded motion policy. */
typedef struct {
    uint32_t schema_version;
    uint8_t enabled;
    uint8_t require_stop_ack;
    uint8_t allow_locomotion;
    uint8_t reserved;
    uint8_t max_speed_percent;
    uint16_t max_runtime_ms;
    uint16_t stop_grace_ms;
    uint32_t revision;
} motion_policy_t;

/** @brief One bounded actuator command in a walking or dancing action. */
typedef struct {
    uint8_t actuator;
    motion_action_t action;
    int16_t angle_degrees;
    uint8_t speed_percent;
} motion_command_t;

/** @brief Stable error codes for diagnostics and guardian-facing copy. */
typedef enum {
    MOTION_OK = 0,
    MOTION_ERR_NULL_ARGUMENT,
    MOTION_ERR_INVALID_POLICY,
    MOTION_ERR_INVALID_COMMAND,
    MOTION_ERR_STORAGE,
    MOTION_ERR_NOT_READY,
} motion_error_t;

/** @brief Return the conservative default motion policy. */
motion_policy_t motion_default(void);

/** @brief Clamp one policy into the documented bounds. */
motion_policy_t motion_clamp(const motion_policy_t *policy);

/** @brief Return true when a policy is version-compatible and bounded. */
bool motion_is_valid(const motion_policy_t *policy);

/** @brief Return the stable string for one motion error code. */
const char *motion_error_name(motion_error_t error);

/** @brief Return the stable string for one action. */
const char *motion_action_name(motion_action_t action);

/** @brief Return the stable string for one lifecycle state. */
const char *motion_state_name(motion_state_t state);

/** @brief Initialize the motion policy store. */
esp_err_t motion_init(void);

/** @brief Return true when the motion policy store is ready. */
bool motion_is_ready(void);

/** @brief Store a guardian policy, clamping out-of-range values. */
esp_err_t motion_set(const motion_policy_t *policy);

/** @brief Return the currently applied policy. */
motion_policy_t motion_get(void);

/** @brief Apply the cached policy to the motion controller. */
esp_err_t motion_apply(const motion_policy_t *policy);

/**
 * @brief Queue one bounded actuator command.
 *
 * A command is rejected when the policy is disabled, the actuator index is out
 * of range, the angle or speed is out of bounds, or the action is locomotion
 * that the policy forbids. Accepted commands are stored for the driver loop.
 */
esp_err_t motion_submit(const motion_command_t *command);

/** @brief Return the number of queued commands. */
size_t motion_queue_depth(void);

/** @brief Return true when a queued command exists for the actuator. */
bool motion_queued_for(uint8_t actuator);

/** @brief Mark an action as started and reset the runtime clock. */
esp_err_t motion_start(motion_action_t action);

/**
 * @brief Advance the controller clock and enforce the safety envelope.
 *
 * Called by the driver loop with the elapsed time since the action started.
 * The controller latches `running` to `stopped` on an explicit stop and to
 * `timeout` when `elapsed_ms` exceeds the bounded maximum run time. A fault
 * latches the controller into the fault state until it is cleared.
 */
motion_state_t motion_tick(uint32_t elapsed_ms);

/** @brief Request a stop; returns true when the stop was accepted. */
bool motion_request_stop(void);

/** @brief Clear a latched stop or timeout and return the controller to idle. */
esp_err_t motion_clear(void);

/** @brief Latch the controller into the fault state and drop the queue. */
esp_err_t motion_fault(void);

/** @brief Return the current lifecycle state. */
motion_state_t motion_state(void);

/**
 * @brief Return the action the controller is currently running.
 *
 * The controller drops the action back to idle on stop, timeout or fault, so
 * the reported action always matches the running lifecycle state.
 */
motion_action_t motion_current_action(void);

/**
 * @brief Return true when communication loss must force a safe stop.
 *
 * A lost link always stops locomotion so a disconnected device never keeps
 * driving; a non-locomotion action is stopped only when the policy demands a
 * stop acknowledgement.
 */
bool motion_should_stop_on_disconnect(const motion_policy_t *policy);

/** @brief Return the removable-module descriptor for motion_control. */
const module_descriptor_t *motion_module_descriptor(void);

#ifdef __cplusplus
}
#endif
