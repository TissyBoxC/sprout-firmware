#include "battery_power.h"

#include <stddef.h>
#include <string.h>

#include "battery_core.h"
#include "config_store.h"

// One bounded binary blob persists the guardian power policy without a
// dedicated partition; a schema change is detected and replaced with the
// conservative default instead of applied.
#define BATTERY_STORE_KEY "battery_policy"

static battery_policy_t s_policy;
static battery_snapshot_t s_snapshot;
static bool s_ready;

static esp_err_t battery_persist(void) {
    const esp_err_t stored = config_store_set_blob(
        BATTERY_STORE_KEY,
        &s_policy,
        sizeof(s_policy)
    );
    if (stored != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static void battery_restore(void) {
    battery_policy_t restored;
    size_t size = sizeof(restored);
    const esp_err_t read = config_store_get_blob(
        BATTERY_STORE_KEY,
        &restored,
        &size
    );
    if (read == ESP_OK && size == sizeof(restored) &&
        battery_is_valid(&restored)) {
        s_policy = restored;
        return;
    }
    s_policy = battery_default();
}

esp_err_t battery_init(void) {
    battery_restore();
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.charge_state = BATTERY_CHARGE_UNKNOWN;
    s_ready = true;
    return ESP_OK;
}

bool battery_is_ready(void) {
    return s_ready;
}

esp_err_t battery_apply(const battery_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL || !battery_is_valid(policy)) {
        return ESP_ERR_INVALID_ARG;
    }
    // The bounded policy is the execution contract; the power manager only
    // consumes validated fields from here.
    s_policy = *policy;
    return ESP_OK;
}

esp_err_t battery_set(const battery_policy_t *policy) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_policy = battery_clamp(policy);
    return battery_persist();
}

battery_policy_t battery_get(void) {
    return s_policy;
}

void battery_report_snapshot(const battery_snapshot_t *snapshot) {
    if (!s_ready || snapshot == NULL) {
        return;
    }
    battery_snapshot_t bounded = *snapshot;
    if (bounded.percent > 100u) {
        bounded.percent = 100u;
    }
    if (bounded.millivolts < 0) {
        bounded.millivolts = 0;
    }
    if (bounded.millivolts > 6000) {
        bounded.millivolts = 6000;
    }
    s_snapshot = bounded;
}

battery_snapshot_t battery_get_snapshot(void) {
    return s_snapshot;
}

const char *battery_charge_state_name(battery_charge_state_t state) {
    switch (state) {
        case BATTERY_CHARGE_DISCHARGING:
            return "discharging";
        case BATTERY_CHARGE_CHARGING:
            return "charging";
        case BATTERY_CHARGE_FULL:
            return "full";
        case BATTERY_CHARGE_UNKNOWN:
        default:
            return "unknown";
    }
}

const char *battery_power_mode_name(battery_power_mode_t mode) {
    switch (mode) {
        case BATTERY_MODE_LOW_POWER:
            return "low_power";
        case BATTERY_MODE_DEEP_SLEEP:
            return "deep_sleep";
        case BATTERY_MODE_NORMAL:
        default:
            return "normal";
    }
}

const char *battery_error_name(battery_error_t error) {
    switch (error) {
        case BATTERY_OK:
            return "ok";
        case BATTERY_ERR_NULL_ARGUMENT:
            return "not_initialized";
        case BATTERY_ERR_INVALID_POLICY:
            return "invalid_policy";
        case BATTERY_ERR_STORAGE:
            return "storage";
        default:
            return "unknown";
    }
}

static esp_err_t battery_initialize(void) {
    return battery_init();
}

const module_descriptor_t *battery_module_descriptor(void) {
    static const module_descriptor_t descriptor = {
        .module_name = "battery_power",
        .version = "1.0.0",
        .initialize = battery_initialize,
        .shutdown = NULL,
    };
    return &descriptor;
}
