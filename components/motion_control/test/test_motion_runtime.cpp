#include "motion_control.h"

#include <cassert>

int main() {
    assert(motion_init() == ESP_OK);
    assert(motion_is_ready());

    const motion_policy_t defaults = motion_get();
    assert(defaults.max_runtime_ms == 5000u);

    // A walk is rejected until the policy allows locomotion.
    motion_command_t walk = {0u, MOTION_ACTION_WALK, 30, 40u};
    assert(motion_submit(&walk) == ESP_ERR_INVALID_ARG);

    motion_policy_t locomotion = defaults;
    locomotion.allow_locomotion = 1u;
    locomotion.max_speed_percent = 80u;
    assert(motion_set(&locomotion) == ESP_OK);
    assert(motion_get().max_speed_percent == 80u);

    // The walk is accepted and clamped; a repeat updates the same actuator.
    motion_command_t speedy = {1u, MOTION_ACTION_WALK, 30, 200u};
    assert(motion_submit(&speedy) == ESP_OK);
    assert(motion_queued_for(1u));
    assert(motion_queue_depth() == 1u);
    assert(motion_submit(&speedy) == ESP_OK);
    assert(motion_queue_depth() == 1u);

    // An out-of-range actuator and an unknown command are rejected.
    motion_command_t bad_actuator = {
        (uint8_t)MOTION_MAX_ACTUATORS, MOTION_ACTION_GESTURE, 10, 10u,
    };
    assert(motion_submit(&bad_actuator) == ESP_ERR_INVALID_ARG);
    assert(motion_submit(nullptr) == ESP_ERR_INVALID_ARG);

    // Starting a walk runs; a runtime over the bound latches a timeout.
    assert(motion_start(MOTION_ACTION_WALK) == ESP_OK);
    assert(motion_state() == MOTION_STATE_RUNNING);
    assert(motion_tick(10u) == MOTION_STATE_RUNNING);
    assert(motion_tick(defaults.max_runtime_ms + 1u) == MOTION_STATE_TIMEOUT);
    assert(motion_state() == MOTION_STATE_TIMEOUT);
    assert(motion_queue_depth() == 0u);

    // A timeout only clears through motion_clear.
    assert(motion_clear() == ESP_OK);
    assert(motion_state() == MOTION_STATE_IDLE);

    // An explicit stop latches stopped and cannot be resumed by a tick.
    assert(motion_start(MOTION_ACTION_TAIL_WAG) == ESP_OK);
    assert(motion_request_stop());
    assert(motion_state() == MOTION_STATE_STOPPED);
    assert(motion_tick(1u) == MOTION_STATE_STOPPED);

    // A fault drops the queue and only motion_clear releases it.
    assert(motion_clear() == ESP_OK);
    assert(motion_submit(&speedy) == ESP_OK);
    assert(motion_fault() == ESP_OK);
    assert(motion_state() == MOTION_STATE_FAULT);
    assert(motion_queue_depth() == 0u);
    assert(motion_submit(&speedy) == ESP_ERR_INVALID_STATE);
    assert(!motion_request_stop());
    assert(motion_start(MOTION_ACTION_GESTURE) == ESP_ERR_INVALID_STATE);

    // Disconnect stops locomotion but respects the stop-ack policy for hand
    // gestures.
    assert(motion_clear() == ESP_OK);
    assert(motion_start(MOTION_ACTION_TAIL_WAG) == ESP_OK);
    assert(motion_should_stop_on_disconnect(&locomotion));
    motion_policy_t no_ack = locomotion;
    no_ack.require_stop_ack = 0u;
    assert(!motion_should_stop_on_disconnect(&no_ack));
    assert(motion_start(MOTION_ACTION_WALK) == ESP_OK);
    assert(motion_should_stop_on_disconnect(&no_ack));

    // Persistence round trip keeps the edited policy.
    motion_policy_t persisted = no_ack;
    persisted.allow_locomotion = 0u;
    assert(motion_set(&persisted) == ESP_OK);
    assert(motion_init() == ESP_OK);
    assert(motion_get().allow_locomotion == 0u);

    // A policy that is not version-compatible is refused by apply.
    motion_policy_t invalid;
    invalid.schema_version = 42u;
    assert(motion_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(motion_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = motion_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
