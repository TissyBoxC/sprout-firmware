#include "motion_report_core.h"

#include <cassert>

int main() {
    // An enabled, idle controller reports idle with the actuator count and no
    // fault.
    motion_policy_t enabled = motion_default();
    enabled.enabled = 1u;
    motion_status_t idle = motion_report_build(
        &enabled,
        MOTION_STATE_IDLE,
        MOTION_ACTION_IDLE,
        4
    );
    assert(idle.state == MOTION_STATE_IDLE);
    assert(idle.action == MOTION_ACTION_IDLE);
    assert(idle.actuator_count == 4);
    assert(idle.faulted == false);

    // A running action is reported with its action name source and no fault.
    motion_status_t running = motion_report_build(
        &enabled,
        MOTION_STATE_RUNNING,
        MOTION_ACTION_WALK,
        8
    );
    assert(running.state == MOTION_STATE_RUNNING);
    assert(running.action == MOTION_ACTION_WALK);
    assert(running.faulted == false);

    // A faulted controller reports faulted regardless of the stored action.
    motion_status_t faulted = motion_report_build(
        &enabled,
        MOTION_STATE_FAULT,
        MOTION_ACTION_DANCE,
        8
    );
    assert(faulted.state == MOTION_STATE_FAULT);
    assert(faulted.faulted == true);

    // A stopped or timed-out controller is not reported as faulted.
    motion_status_t stopped = motion_report_build(
        &enabled,
        MOTION_STATE_STOPPED,
        MOTION_ACTION_IDLE,
        8
    );
    assert(stopped.faulted == false);
    motion_status_t timeout = motion_report_build(
        &enabled,
        MOTION_STATE_TIMEOUT,
        MOTION_ACTION_IDLE,
        8
    );
    assert(timeout.faulted == false);

    // A disabled controller always reports idle and unfaulted so a turned-off
    // body never reads as running or faulted.
    motion_policy_t disabled = motion_default();
    disabled.enabled = 0u;
    motion_status_t off = motion_report_build(
        &disabled,
        MOTION_STATE_RUNNING,
        MOTION_ACTION_WALK,
        8
    );
    assert(off.state == MOTION_STATE_IDLE);
    assert(off.action == MOTION_ACTION_IDLE);
    assert(off.faulted == false);

    // The actuator count is clamped into the platform bound and never
    // negative, and a NULL policy yields the conservative default.
    motion_status_t over = motion_report_build(
        &enabled,
        MOTION_STATE_IDLE,
        MOTION_ACTION_IDLE,
        99
    );
    assert(over.actuator_count == 32);
    motion_status_t negative = motion_report_build(
        &enabled,
        MOTION_STATE_IDLE,
        MOTION_ACTION_IDLE,
        -5
    );
    assert(negative.actuator_count == 0);
    motion_status_t null_status = motion_report_build(
        nullptr,
        MOTION_STATE_RUNNING,
        MOTION_ACTION_WALK,
        6
    );
    assert(null_status.state == MOTION_STATE_IDLE);
    assert(null_status.action == MOTION_ACTION_IDLE);
    assert(null_status.actuator_count == 6);
    assert(null_status.faulted == false);

    return 0;
}
