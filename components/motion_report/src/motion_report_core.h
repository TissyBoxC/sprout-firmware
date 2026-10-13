#pragma once

#include <stdbool.h>

#include "motion_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One bounded motion status ready to report to the platform. */
typedef struct {
    motion_state_t state;
    motion_action_t action;
    int actuator_count;
    bool faulted;
} motion_status_t;

/**
 * @brief Build a bounded motion status from the live controller state.
 *
 * A disabled controller reports idle with no fault, so a body that is turned
 * off never reads as running or faulted. The fault flag is derived from the
 * lifecycle state rather than stored separately, so the reported status is
 * always self-consistent. The actuator count is clamped into the platform
 * bound and never negative.
 */
motion_status_t motion_report_build(
    const motion_policy_t *policy,
    motion_state_t state,
    motion_action_t action,
    int actuator_count
);

#ifdef __cplusplus
}
#endif
