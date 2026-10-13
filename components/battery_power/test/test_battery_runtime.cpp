#include "battery_power.h"

#include <cassert>

int main() {
    assert(battery_init() == ESP_OK);
    assert(battery_is_ready());

    const battery_policy_t defaults = battery_get();
    assert(defaults.sleep_after_seconds == 1800u);

    battery_policy_t requested = defaults;
    requested.sleep_after_seconds = 1u;
    requested.low_battery_warn_percent = 99u;
    assert(battery_set(&requested) == ESP_OK);
    const battery_policy_t stored = battery_get();
    assert(stored.sleep_after_seconds == BATTERY_MIN_SLEEP_AFTER_SECONDS);
    assert(stored.low_battery_warn_percent == BATTERY_MAX_WARN_PERCENT);

    battery_snapshot_t snapshot = {BATTERY_CHARGE_FULL, 100u, 4200, true};
    battery_report_snapshot(&snapshot);
    const battery_snapshot_t reported = battery_get_snapshot();
    assert(reported.percent == 100u);
    assert(reported.charge_state == BATTERY_CHARGE_FULL);

    battery_snapshot_t out_of_range = {BATTERY_CHARGE_CHARGING, 250u, 9000, true};
    battery_report_snapshot(&out_of_range);
    const battery_snapshot_t bounded = battery_get_snapshot();
    assert(bounded.percent == 100u);
    assert(bounded.millivolts == 6000);

    battery_policy_t persisted = stored;
    persisted.deep_sleep_enabled = 0u;
    persisted.revision = 12u;
    assert(battery_set(&persisted) == ESP_OK);
    assert(battery_init() == ESP_OK);
    assert(battery_get().deep_sleep_enabled == 0u);
    assert(battery_get().revision == 12u);

    battery_policy_t invalid;
    invalid.schema_version = 42u;
    assert(battery_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(battery_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = battery_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
