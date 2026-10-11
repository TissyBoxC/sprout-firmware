#include "display_policy.h"

#include <cassert>

int main() {
    assert(display_policy_init() == ESP_OK);
    assert(display_policy_is_ready());

    const display_policy_t defaults = display_policy_get();
    assert(defaults.brightness_percent == 60u);

    display_policy_t requested = defaults;
    requested.brightness_percent = 200u;
    requested.daily_limit_minutes = 9999u;
    assert(display_policy_set(&requested) == ESP_OK);
    const display_policy_t stored = display_policy_get();
    assert(stored.brightness_percent == DISPLAY_POLICY_MAX_BRIGHTNESS);
    assert(stored.daily_limit_minutes ==
           DISPLAY_POLICY_MAX_DAILY_LIMIT_MINUTES);

    const uint32_t base = 1700000000u;
    display_policy_reset_usage(base);
    for (uint32_t second = 0; second < 1200u; ++second) {
        (void)display_policy_report_activity(base + second);
    }
    assert(display_policy_break_due(base + 1199u) == false);
    assert(display_policy_break_due(base + 1200u) == true);

    assert(display_policy_daily_limit_reached(
        600u * 60u,
        base
    ) == true);
    assert(display_policy_daily_limit_reached(
        599u * 60u,
        base
    ) == false);

    display_policy_t invalid;
    invalid.schema_version = 42u;
    assert(display_policy_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(display_policy_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor =
        display_policy_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
