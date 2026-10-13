#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "battery_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers, free of ESP-IDF dependencies so the policy mapping is covered
 * by host tests. */

/**
 * @brief Build one applied power policy from the platform response.
 *
 * Every field is clamped into the shared bounds by battery_clamp, so a
 * malformed or out-of-range platform value can never install an unsafe sleep
 * window. A negative revision is treated as zero.
 */
battery_policy_t power_policy_sync_core_build(
    bool enabled,
    bool low_power_enabled,
    bool deep_sleep_enabled,
    bool charge_priority,
    int low_battery_warn_percent,
    int low_power_enter_percent,
    int sleep_after_seconds,
    int revision
);

#ifdef __cplusplus
}
#endif
