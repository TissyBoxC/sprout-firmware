#pragma once

#include <stdbool.h>

#include "battery_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One bounded power status ready to report to the platform. */
typedef struct {
    battery_charge_state_t charge_state;
    battery_power_mode_t power_mode;
    int battery_percent;
    int millivolts;
    bool charging;
} power_status_t;

/**
 * @brief Build a bounded power status from the live battery snapshot.
 *
 * The charge state, percentage and voltage are clamped into the platform
 * contract, and the reported power mode is derived from the same policy the
 * device applies, so the guardian overview never sees an out-of-range value or
 * a mode the device is not actually running. A device with no fresh
 * measurement reports the unknown charge state at zero so a missing gauge
 * never reads as a full battery.
 */
power_status_t power_report_build(
    const battery_policy_t *policy,
    const battery_snapshot_t *snapshot,
    bool idle_elapsed
);

#ifdef __cplusplus
}
#endif
