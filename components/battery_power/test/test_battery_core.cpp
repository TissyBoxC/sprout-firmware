#include "battery_core.h"
#include "battery_power.h"

#include <cassert>

int main() {
    const battery_policy_t defaults = battery_default();
    assert(battery_is_valid(&defaults));
    assert(defaults.low_battery_warn_percent == 20u);
    assert(defaults.sleep_after_seconds == 1800u);

    battery_policy_t out_of_range = defaults;
    out_of_range.low_battery_warn_percent = 0u;
    out_of_range.low_power_enter_percent = 99u;
    out_of_range.sleep_after_seconds = 1u;
    const battery_policy_t clamped = battery_clamp(&out_of_range);
    assert(clamped.low_battery_warn_percent == BATTERY_MIN_WARN_PERCENT);
    assert(clamped.low_power_enter_percent == BATTERY_MAX_LOW_POWER_PERCENT);
    assert(clamped.sleep_after_seconds == BATTERY_MIN_SLEEP_AFTER_SECONDS);
    assert(battery_is_valid(&clamped));

    assert(!battery_is_valid(nullptr));
    battery_policy_t wrong_version = defaults;
    wrong_version.schema_version = 99u;
    assert(!battery_is_valid(&wrong_version));
    assert(battery_clamp(nullptr).sleep_after_seconds ==
           defaults.sleep_after_seconds);

    assert(battery_derive_charge_state(false, false) ==
           BATTERY_CHARGE_DISCHARGING);
    assert(battery_derive_charge_state(true, false) ==
           BATTERY_CHARGE_CHARGING);
    assert(battery_derive_charge_state(true, true) == BATTERY_CHARGE_FULL);

    battery_snapshot_t snapshot = {BATTERY_CHARGE_DISCHARGING, 10u, 3400, true};
    assert(battery_low_warning(&defaults, &snapshot));
    snapshot.percent = 50u;
    assert(!battery_low_warning(&defaults, &snapshot));

    // Charging returns to normal even at a low percentage.
    snapshot.percent = 5u;
    snapshot.charge_state = BATTERY_CHARGE_CHARGING;
    assert(battery_decide_mode(&defaults, &snapshot, true) ==
           BATTERY_MODE_NORMAL);

    // Discharging at the entry threshold enters low power, then deep sleep
    // once the idle window elapses.
    snapshot.charge_state = BATTERY_CHARGE_DISCHARGING;
    assert(battery_decide_mode(&defaults, &snapshot, false) ==
           BATTERY_MODE_LOW_POWER);
    assert(battery_decide_mode(&defaults, &snapshot, true) ==
           BATTERY_MODE_DEEP_SLEEP);

    // A healthy pack stays normal.
    snapshot.percent = 80u;
    assert(battery_decide_mode(&defaults, &snapshot, true) ==
           BATTERY_MODE_NORMAL);

    // A missing reading keeps the device normal.
    battery_snapshot_t no_reading = {
        BATTERY_CHARGE_UNKNOWN, 0u, 0, false,
    };
    assert(battery_decide_mode(&defaults, &no_reading, true) ==
           BATTERY_MODE_NORMAL);

    // Deep sleep retains the configured wake source; disabling the mode drops
    // it.
    assert(battery_wake_source_retained(&defaults, BATTERY_MODE_DEEP_SLEEP));
    assert(battery_wake_source_retained(&defaults, BATTERY_MODE_LOW_POWER));
    battery_policy_t no_sleep = defaults;
    no_sleep.deep_sleep_enabled = 0u;
    assert(!battery_wake_source_retained(&no_sleep, BATTERY_MODE_DEEP_SLEEP));

    // The estimator averages samples and rounds to the nearest percent.
    battery_average_t average;
    battery_average_init(&average);
    assert(battery_average_add(&average, 40u) == 40u);
    assert(battery_average_add(&average, 50u) == 45u);
    assert(battery_average_add(&average, 61u) == 50u);
    return 0;
}
