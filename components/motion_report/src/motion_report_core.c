#include "motion_report_core.h"

#include <stddef.h>

// The platform bounds an actuator count at 32, which is above the firmware
// channel limit, so a report is clamped to the platform contract.
#define MOTION_REPORT_MAX_ACTUATORS 32

motion_status_t motion_report_build(
    const motion_policy_t *policy,
    motion_state_t state,
    motion_action_t action,
    int actuator_count
) {
    motion_status_t status = {
        .state = MOTION_STATE_IDLE,
        .action = MOTION_ACTION_IDLE,
        .actuator_count = 0,
        .faulted = false,
    };
    if (actuator_count > 0) {
        status.actuator_count = actuator_count > MOTION_REPORT_MAX_ACTUATORS
            ? MOTION_REPORT_MAX_ACTUATORS
            : actuator_count;
    }
    if (policy == NULL || policy->enabled == 0u) {
        return status;
    }
    status.state = state;
    status.action = action;
    status.faulted = state == MOTION_STATE_FAULT;
    return status;
}
