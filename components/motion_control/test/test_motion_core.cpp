#include "motion_control.h"
#include "motion_core.h"

#include <cassert>

int main() {
    const motion_policy_t defaults = motion_default();
    assert(motion_is_valid(&defaults));
    assert(defaults.max_runtime_ms == 5000u);
    assert(defaults.max_speed_percent == 60u);
    assert(defaults.allow_locomotion == 0u);

    motion_policy_t out_of_range = defaults;
    out_of_range.max_speed_percent = 200u;
    out_of_range.max_runtime_ms = 1u;
    const motion_policy_t clamped = motion_clamp(&out_of_range);
    assert(clamped.max_speed_percent == MOTION_MAX_SPEED_PERCENT);
    assert(clamped.max_runtime_ms == MOTION_MIN_MAX_RUNTIME_MS);
    assert(motion_is_valid(&clamped));

    assert(!motion_is_valid(nullptr));
    motion_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!motion_is_valid(&wrong_version));
    assert(motion_clamp(nullptr).max_runtime_ms == defaults.max_runtime_ms);

    assert(motion_actuator_valid(0u));
    assert(motion_actuator_valid(MOTION_MAX_ACTUATORS - 1u));
    assert(!motion_actuator_valid(MOTION_MAX_ACTUATORS));

    assert(motion_clamp_angle(-40) == MOTION_MIN_ANGLE_DEGREES);
    assert(motion_clamp_angle(400) == MOTION_MAX_ANGLE_DEGREES);
    assert(motion_clamp_angle(90) == 90);

    // Locomotion is walk and dance; tail and gesture are not.
    assert(motion_action_is_locomotion(MOTION_ACTION_WALK));
    assert(motion_action_is_locomotion(MOTION_ACTION_DANCE));
    assert(!motion_action_is_locomotion(MOTION_ACTION_TAIL_WAG));
    assert(!motion_action_is_locomotion(MOTION_ACTION_GESTURE));
    assert(!motion_action_is_locomotion(MOTION_ACTION_IDLE));

    // A command is clamped to the policy speed ceiling and the angle bounds.
    motion_command_t command = {2u, MOTION_ACTION_GESTURE, 400, 100u};
    const motion_command_t bounded = motion_clamp_command(&defaults, &command);
    assert(bounded.actuator == 2u);
    assert(bounded.angle_degrees == MOTION_MAX_ANGLE_DEGREES);
    assert(bounded.speed_percent == defaults.max_speed_percent);

    // The runtime bound trips inclusively.
    assert(!motion_runtime_exceeded(&defaults, defaults.max_runtime_ms - 1u));
    assert(motion_runtime_exceeded(&defaults, defaults.max_runtime_ms));

    // A null policy falls back to the documented maximum speed.
    motion_command_t fast = {0u, MOTION_ACTION_GESTURE, 90, 100u};
    const motion_command_t unbounded = motion_clamp_command(nullptr, &fast);
    assert(unbounded.speed_percent == MOTION_MAX_SPEED_PERCENT);
    return 0;
}
