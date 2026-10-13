#include "power_report_core.h"

#include <cassert>

static battery_snapshot_t measured_snapshot(
    battery_charge_state_t charge_state,
    uint8_t percent,
    int16_t millivolts
) {
    battery_snapshot_t snapshot = {};
    snapshot.charge_state = charge_state;
    snapshot.percent = percent;
    snapshot.millivolts = millivolts;
    snapshot.has_measurement = true;
    return snapshot;
}

int main() {
    const battery_policy_t policy = battery_default();

    // A discharging pack reports its measured percentage, voltage and the
    // low-power mode the policy would apply at that level.
    battery_snapshot_t low = measured_snapshot(
        BATTERY_CHARGE_DISCHARGING,
        12,
        3600
    );
    power_status_t low_status = power_report_build(&policy, &low, false);
    assert(low_status.charge_state == BATTERY_CHARGE_DISCHARGING);
    assert(low_status.power_mode == BATTERY_MODE_LOW_POWER);
    assert(low_status.battery_percent == 12);
    assert(low_status.millivolts == 3600);
    assert(low_status.charging == false);

    // An idle low pack enters deep sleep, matching the runtime decision.
    power_status_t deep = power_report_build(&policy, &low, true);
    assert(deep.power_mode == BATTERY_MODE_DEEP_SLEEP);

    // A charger keeps the device normal even when the pack is nearly empty.
    battery_snapshot_t charging = measured_snapshot(
        BATTERY_CHARGE_CHARGING,
        8,
        3700
    );
    power_status_t charging_status =
        power_report_build(&policy, &charging, true);
    assert(charging_status.charge_state == BATTERY_CHARGE_CHARGING);
    assert(charging_status.power_mode == BATTERY_MODE_NORMAL);
    assert(charging_status.charging == true);

    // A full pack on the charger is reported as full and normal.
    battery_snapshot_t full = measured_snapshot(
        BATTERY_CHARGE_FULL,
        100,
        4200
    );
    power_status_t full_status = power_report_build(&policy, &full, true);
    assert(full_status.charge_state == BATTERY_CHARGE_FULL);
    assert(full_status.charging == true);
    assert(full_status.battery_percent == 100);
    assert(full_status.power_mode == BATTERY_MODE_NORMAL);

    // A high but healthy discharging pack stays normal.
    battery_snapshot_t healthy = measured_snapshot(
        BATTERY_CHARGE_DISCHARGING,
        80,
        4000
    );
    power_status_t healthy_status =
        power_report_build(&policy, &healthy, true);
    assert(healthy_status.power_mode == BATTERY_MODE_NORMAL);

    // A device with no fresh measurement reports unknown at zero so a missing
    // gauge never reads as a full battery.
    battery_snapshot_t missing = {};
    missing.charge_state = BATTERY_CHARGE_UNKNOWN;
    missing.percent = 100;
    missing.millivolts = 4200;
    missing.has_measurement = false;
    power_status_t missing_status =
        power_report_build(&policy, &missing, false);
    assert(missing_status.charge_state == BATTERY_CHARGE_UNKNOWN);
    assert(missing_status.power_mode == BATTERY_MODE_NORMAL);
    assert(missing_status.battery_percent == 0);
    assert(missing_status.millivolts == 0);
    assert(missing_status.charging == false);

    // A NULL snapshot still yields a bounded, valid default.
    power_status_t null_status = power_report_build(&policy, nullptr, false);
    assert(null_status.charge_state == BATTERY_CHARGE_UNKNOWN);
    assert(null_status.battery_percent == 0);
    assert(null_status.millivolts == 0);

    // Out-of-range measurements are clamped into the platform bounds.
    battery_snapshot_t oversize = measured_snapshot(
        BATTERY_CHARGE_DISCHARGING,
        200,
        9000
    );
    power_status_t oversize_status =
        power_report_build(&policy, &oversize, false);
    assert(oversize_status.battery_percent == 100);
    assert(oversize_status.millivolts == 6000);

    return 0;
}
