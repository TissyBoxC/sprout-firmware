#include "power_report_core.h"

#include <stddef.h>

power_status_t power_report_build(
    const battery_policy_t *policy,
    const battery_snapshot_t *snapshot,
    bool idle_elapsed
) {
    power_status_t status = {
        .charge_state = BATTERY_CHARGE_UNKNOWN,
        .power_mode = BATTERY_MODE_NORMAL,
        .battery_percent = 0,
        .millivolts = 0,
        .charging = false,
    };
    if (snapshot == NULL) {
        return status;
    }
    status.charge_state = snapshot->charge_state;
    status.charging =
        snapshot->charge_state == BATTERY_CHARGE_CHARGING ||
        snapshot->charge_state == BATTERY_CHARGE_FULL;
    if (snapshot->has_measurement) {
        int percent = snapshot->percent;
        if (percent > 100) {
            percent = 100;
        }
        status.battery_percent = percent;
        int millivolts = snapshot->millivolts;
        if (millivolts < 0) {
            millivolts = 0;
        }
        if (millivolts > 6000) {
            millivolts = 6000;
        }
        status.millivolts = millivolts;
    }
    // The reported mode comes from the same decision the runtime applies, so a
    // charging or unmeasured device always reports normal.
    status.power_mode = battery_decide_mode(policy, snapshot, idle_elapsed);
    return status;
}
