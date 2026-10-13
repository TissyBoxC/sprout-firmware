#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "motion_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the policy mapping is covered
 * by host tests. */

/**
 * @brief Build one applied motion policy from the platform response.
 *
 * Every field is clamped into the shared bounds by motion_clamp, so a
 * malformed or out-of-range platform value can never install an unsafe speed,
 * runtime or stop-grace envelope. A negative revision is treated as zero.
 */
motion_policy_t motion_policy_sync_core_build(
    bool enabled,
    bool require_stop_ack,
    bool allow_locomotion,
    int max_speed_percent,
    int max_runtime_ms,
    int stop_grace_ms,
    int revision
);

#ifdef __cplusplus
}
#endif
