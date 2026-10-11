#include "cellular_4g.h"

#include <cassert>

int main() {
    assert(cellular_init() == ESP_OK);
    assert(cellular_is_ready());

    const cellular_policy_t defaults = cellular_get();
    assert(defaults.monthly_quota_mb == 2048u);

    cellular_policy_t requested = defaults;
    requested.monthly_quota_mb = 9999999u;
    requested.keepalive_seconds = 1u;
    assert(cellular_set(&requested) == ESP_OK);
    const cellular_policy_t stored = cellular_get();
    assert(stored.monthly_quota_mb == CELLULAR_MAX_QUOTA_MB);
    assert(stored.keepalive_seconds == CELLULAR_MIN_KEEPALIVE_SECONDS);

    cellular_policy_t persisted = stored;
    persisted.roaming_allowed = 1u;
    assert(cellular_set(&persisted) == ESP_OK);
    assert(cellular_init() == ESP_OK);
    assert(cellular_get().roaming_allowed == 1u);

    cellular_policy_t invalid;
    invalid.schema_version = 42u;
    assert(cellular_apply(&invalid) == ESP_ERR_INVALID_ARG);
    assert(cellular_set(nullptr) == ESP_ERR_INVALID_ARG);

    const module_descriptor_t *descriptor = cellular_module_descriptor();
    assert(descriptor != nullptr);
    assert(descriptor->initialize() == ESP_OK);
    return 0;
}
