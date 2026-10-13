#include "motion_policy_sync_core.h"

#include <cassert>

int main() {
    // A well-formed platform policy maps onto the firmware contract.
    const motion_policy_t policy = motion_policy_sync_core_build(
        true,
        false,
        true,
        75,
        4000,
        250,
        9
    );
    assert(policy.schema_version == MOTION_SCHEMA_VERSION);
    assert(policy.enabled == 1u);
    assert(policy.require_stop_ack == 0u);
    assert(policy.allow_locomotion == 1u);
    assert(policy.max_speed_percent == 75u);
    assert(policy.max_runtime_ms == 4000u);
    assert(policy.stop_grace_ms == 250u);
    assert(policy.revision == 9u);
    assert(motion_is_valid(&policy));

    // Out-of-range values are clamped instead of installed verbatim, and a
    // negative revision is treated as zero.
    const motion_policy_t clamped = motion_policy_sync_core_build(
        true,
        true,
        false,
        0,
        1,
        99999,
        -4
    );
    assert(clamped.max_speed_percent == MOTION_MIN_SPEED_PERCENT);
    assert(clamped.max_runtime_ms == MOTION_MIN_MAX_RUNTIME_MS);
    assert(clamped.stop_grace_ms == MOTION_MAX_MAX_RUNTIME_MS);
    assert(clamped.revision == 0u);
    assert(motion_is_valid(&clamped));

    return 0;
}
